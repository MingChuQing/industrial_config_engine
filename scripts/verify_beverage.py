"""Independent static behavior-contract comparison (not a hardware test).

Reads both formats separately. Expands L4 -> L3 -> L2 -> L1 references and
binds typed arguments. Compares device-addressed requests, responses, order,
waits, polling contracts, decoders, predicates, error identities, caches and UI
updates. Does not infer execution equivalence from source size or frame counts.
"""
import argparse
from collections import Counter
import copy
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def read(path):
    def pairs(items):
        out = {}
        for key, value in items:
            if key in out or key != key.strip():
                raise ValueError(f"Duplicate or whitespace-padded JSON key: {key!r}")
            out[key] = value
        return out
    return json.loads(path.read_text(encoding="utf-8-sig"), object_pairs_hook=pairs)


def require(condition, message):
    if not condition:
        raise ValueError(message)


def hexframe(value):
    return bytes.fromhex(value).hex(" ").upper()


def legacy_events(obj):
    if "actions" in obj:
        return [v for a in obj["actions"] for v in legacy_events(a)]
    t = obj["type"]
    if t == "timer":
        return [{"kind": "wait", "ms": obj["seconds"] * 1000}]
    if t == "updateUI":
        return [{"kind": "ui", "target": obj["uiTarget"], "value": obj["uiValue"]}]
    base = {"device": obj["device"], "request": hexframe(obj["request"])}
    if t == "autorequestresponse":
        return [{"kind": "write", **base, "response": hexframe(obj["response"])}]
    if t == "loopUntilResponse":
        return [{"kind": "poll", **base, "expected_response": hexframe(obj["expectedResponse"]),
                 "interval_ms": obj["intervalMs"], "timeout_ms": obj["timeoutMs"],
                 "error_code": obj["errorCode"], "error_message": obj["timeoutMsg"]}]
    require(t in ("readAndCache", "readAndCompare"), f"Unknown legacy operation {t}")
    prefix = "cache" if t == "readAndCache" else "value"
    parse = {"start_byte": obj[prefix + "StartByte"], "length": obj[prefix + "Length"],
             "endian": obj[prefix + "Endian"], "type": obj["valueType"]}
    if t == "readAndCache":
        return [{"kind": "cache", **base, "parse": parse, "cache_key": obj["cacheKey"]}]
    return [{"kind": "compare", **base, "parse": parse,
             "condition": {"equal": "eq", "greaterOrEqual": "ge"}[obj["compareType"]],
             "value": obj["compareValue"], "error_code": obj["errorCode"],
             "error_message": obj["errorMessage"]}]


class Layered:
    def __init__(self, directory):
        self.root = directory.resolve()
        self.actions = {v["filename"]: v for v in read(directory / "L1_action/all_actions.json")["actions"]}
        self.nodes = {v["filename"]: v for v in read(directory / "L2_node/all_nodes.json")["nodes"]}
        # This beverage profile uses FC03/04/05/06 PDUs. The station byte and
        # RTU CRC belong to the transport adapter, never the reusable L1 action.
        for action in self.actions.values():
            if not action["type"].startswith("modbus_"):
                continue
            require(not ({"device", "slave_id", "crc"} & action.keys()), "Transport fields in L1")
            require(not any(a["name"] in ("device", "slave_id", "crc") for a in action.get("args", [])),
                    "Transport argument in L1")
            for field in ("request", "response"):
                if field not in action:
                    continue
                tokens = action[field].split()
                require(tokens and tokens[0] in ("03", "04", "05", "06"),
                        "Expected PDU function code first: " + action["filename"])
                if field == "response" and tokens[0] in ("03", "04"):
                    require(len(tokens) >= 2 and len(tokens) == 2 + int(tokens[1], 16),
                            "Read response PDU length differs from byte count: " + action["filename"])
                else:
                    require(len(tokens) == 5,
                            "Expected FC03/04 request or FC05/06 echo without station/CRC: " + action["filename"])
        for node in self.nodes.values():
            action = self.actions[node["action"]["template"].rsplit("/", 1)[1]]
            if action["type"].startswith("modbus_"):
                require(isinstance(node.get("device"), str) and node["device"].strip(), "Missing L2 device field")
        self.flow = read(directory / "L4_flow/main_flow.json")
        self.used_actions = set()
        self.used_nodes = set()
        self.used_groups = set()
        self.group_calls = Counter()
        self.cache_types = {}
        self.group_args = read(directory / "system_variables.json")
        points = self.group_args["production_points"]
        require(isinstance(points, list) and 0 < len(points) <= 101, "Production point table must fit 0..100")
        for point in points:
            require(all(type(point.get(a)) is int and 0 <= point[a] <= 15 for a in "xyz"), "Full XYZ PR tuple required")
        require(all(self.group_args.get("last_" + a + "_pr") == -1 for a in "xyz"), "Startup PR cache must be unknown")
        self.device_config = read(directory / "device_registry.json")["devices"]
        endpoints = set()
        for alias, endpoint in self.device_config.items():
            require(isinstance(endpoint.get("port"), str) and endpoint["port"].strip(), "Missing device port")
            require(type(endpoint.get("address")) is int and 1 <= endpoint["address"] <= 247, "Invalid station address")
            key = (endpoint["port"], endpoint["address"])
            require(key not in endpoints, "Duplicate device port/address")
            endpoints.add(key)
        self.calculations = 0

    def lookup_variable(self, key):
        if key in self.group_args:
            return self.group_args[key]
        root = re.match(r"^[^.\[]+", key)
        require(root is not None and root[0] in self.group_args, "Undefined variable: " + key)
        value = self.group_args[root[0]]
        tail = key[len(root[0]):]
        while tail:
            member = re.match(r"^\.([^.\[]+)|^\[([^\]]+)\]", tail)
            require(member is not None, "Invalid variable path: " + key)
            if member[1] is not None:
                require(isinstance(value, dict) and member[1] in value, "Missing member: " + key)
                value = value[member[1]]
            else:
                token = member[2]
                index = int(token) if token.isdecimal() else self.group_args.get(token)
                require(type(index) is int and isinstance(value, list) and 0 <= index < len(value), "Invalid array index: " + key)
                value = value[index]
            tail = tail[len(member[0]):]
        return value

    def resolve_arg(self, value):
        if isinstance(value, str):
            match = re.fullmatch(r"\$\{([^}]+)\}", value)
            if match and match[1] not in self.cache_types:
                return self.lookup_variable(match[1])
        return value

    def lookup(self, reference, directory, values):
        file, key = reference.rsplit("/", 1)
        require(file == directory + "/all_" + ("actions" if directory == "L1_action" else "nodes") + ".json",
                f"Unexpected bundle reference: {reference}")
        require(key in values, f"Missing reference: {reference}")
        return values[key]

    def decode_node(self, item):
        require(item["type"] == "node", "Expected a node")
        node = self.lookup(self.resolve_arg(item["template"]), "L2_node", self.nodes)
        action = self.lookup(node["action"]["template"], "L1_action", self.actions)
        self.used_nodes.add(node["filename"])
        self.used_actions.add(action["filename"])
        params = item.get("params") or node.get("params", [])
        declarations = action.get("args", [])
        require(len(params) == len(declarations), "Argument count mismatch: " + node["filename"])
        for definition in (node, action):
            prefix = definition["filename"].split(".r_", 1)[0]
            signature_types = prefix.split(".", 1)[1] if "." in prefix else ""
            require(signature_types == "_".join(a["type"] for a in declarations),
                    "Naming-contract argument types differ: " + definition["filename"])
        bound = {}
        for value, arg in zip(params, declarations):
            value = self.resolve_arg(value)
            typ = arg["type"]
            if typ == "s":
                require(isinstance(value, str), f"String required for {arg['name']}")
                for key in re.findall(r"\$\{([^}]+)\}", value):
                    require(key in self.cache_types, "Cache referenced before definition: " + key)
            elif typ == "i32":
                require(type(value) is int and -(2**31) <= value < 2**31, "Signed integer required")
            elif typ.startswith("u"):
                require(type(value) is int and 0 <= value < 2 ** int(typ[1:]), f"Out-of-range {typ}: {value}")
                require(arg.get("min", 0) <= value <= arg.get("max", 2 ** int(typ[1:]) - 1),
                        "Argument declaration range exceeded: " + arg["name"])
            else:
                raise ValueError("Unsupported argument type: " + typ)
            bound[arg["name"]] = value
        if action["type"].startswith("modbus_"):
            device = self.resolve_arg(item.get("device", node.get("device")))
            require(isinstance(device, str) and device in self.device_config, "Missing or unknown L2 device binding")
            bound["slave_id"] = device
        def substitute(text):
            def replace(match):
                name = match[1]
                if name.endswith("_high") or name.endswith("_low"):
                    base, part = name.rsplit("_", 1)
                    require(base in bound, "Unbound frame placeholder: " + name)
                    value = bound[base]
                    return f"{(value >> 8 if part == 'high' else value) & 255:02X}"
                require(name in bound, "Unbound placeholder: " + name)
                return str(bound[name])
            return re.sub(r"\$\{([^}]+)\}", replace, text)
        return node, action, bound, substitute

    def atomic(self, item):
        node, action, bound, substitute = self.decode_node(item)
        t = action["type"]
        if t == "set_variable":
            self.group_args[bound["key"]] = bound["value"]
            return None
        if t == "read_variable":
            self.group_args[item["result_key"]] = self.lookup_variable(bound["key"])
            return None
        if t == "calculate":
            match = re.fullmatch(r"([0-9]+) \+ \$\{param_0\}", action.get("expression", ""))
            require(match is not None and len(bound) == 1, "Unsupported calculation")
            result = int(match[1]) + next(iter(bound.values()))
            self.group_args[item["result_key"]] = result
            self.calculations += 1
            return None  # internal computation, no device/UI/wait operation
        if t == "wait":
            require(action.get("unit", "ms") == "ms", "Wait unit differs")
            return {"kind": "wait", "ms": bound["ms"]}
        if t == "ui_action":
            require(action["action"] == "update_status", "UI action differs")
            return {"kind": "ui", "target": substitute(action["data"]["target"]),
                    "value": substitute(action["data"]["value"])}
        require("slave_id" in bound, "Device identity must be explicit")
        base = {"device": bound["slave_id"], "request": hexframe(substitute(action["request"]))}
        if t == "modbus_write_verify":
            if "result_key" in item:
                require(node.get("judge") == {"type": "exists"}, "Trigger must acknowledge success before caching")
                self.group_args[item["result_key"]] = True
            return {"kind": "write", **base, "response": hexframe(substitute(action["response"]))}
        require(t == "modbus_read_cache", "Unknown action: " + t)
        if "judge" in node:
            judge = node["judge"]
            require(judge["type"] == "compare", "Expected comparison predicate")
            failure = node.get("on_failure", [])
            require(len(failure) == 1 and failure[0]["type"] == "popup", "Missing failure report")
            return {"kind": "compare", **base, "parse": action["parse"],
                    "condition": judge["condition"], "value": judge["value"],
                    "error_code": failure[0]["error_code"], "error_message": failure[0]["message"]}
        require("result_key" in item, "Cache destination absent")
        result_type = action["filename"].split(".r_", 1)[1].split("_", 1)[0]
        require(node["filename"].split(".r_", 1)[1].split("_", 1)[0] == result_type,
                "Cache return types differ")
        if action["parse"]["type"] == "int" and action["parse"]["length"] == 4:
            require(result_type == "i32", "Signed 32-bit cache return type required")
        self.cache_types[item["result_key"]] = result_type
        return {"kind": "cache", **base, "parse": action["parse"], "cache_key": item["result_key"]}

    def group(self, item, ancestors=()):
        require(item["type"] == "group", "Only group/node composition is allowed")
        if "template" in item:
            path = (self.root / item["template"]).resolve()
            require(path.is_relative_to(self.root / "L3_group"), "Group path escapes L3")
            require(path not in ancestors, "Cyclic group reference")
            self.used_groups.add(path)
            self.group_calls[item["template"]] += 1
            definition = read(path)
            declarations = definition.get("args", [])
            params = item.get("params", [])
            require(len(declarations) == len(params), "Group argument count mismatch: " + str(path))
            for i, (arg, value) in enumerate(zip(declarations, params)):
                require(arg["index"] == i, "Group argument index mismatch")
                value = self.resolve_arg(value)
                if arg["type"] == "s":
                    require(isinstance(value, str) and "${" not in value, "Unresolved group argument")
                elif arg["type"] == "arr":
                    require(isinstance(value, list), "Array group argument required")
                else:
                    typ = arg["type"]
                    require(typ in ("u16", "u32") and type(value) is int and 0 <= value < 2 ** int(typ[1:]),
                            "Invalid numeric group argument")
                if "min" in arg or "max" in arg:
                    require(type(value) is int and arg.get("min", value) <= value <= arg.get("max", value), "Group argument range exceeded")
                self.group_args[arg["name"]] = value
            return self.group(definition, ancestors + (path,))
        require(item.get("name"), "Unnamed group")
        mode = item.get("mode")
        if mode == "sequence":
            return [event for child in item["body"] for event in
                    ([self.atomic(child)] if child["type"] == "node" else self.group(child, ancestors)) if event is not None]
        if mode == "if":
            condition = item["condition"]
            require(condition.get("type") == "expression", "Expected expression guard")
            match = re.fullmatch(r"(\$\{[^}]+\}) (==|!=) (\$\{[^}]+\}|true|false|-?\d+)", condition["expression"])
            require(match is not None, "Unsupported movement guard")
            lhs = self.resolve_arg(match[1])
            rhs = self.resolve_arg(match[3]) if match[3].startswith("${") else json.loads(match[3])
            selected = lhs == rhs if match[2] == "==" else lhs != rhs
            return [event for child in item["then" if selected else "else"] for event in
                    ([self.atomic(child)] if child["type"] == "node" else self.group(child, ancestors)) if event is not None]
        require(mode == "loop", "Unexpected group mode: " + str(mode))
        loop = item["loop"]
        if loop["type"] == "foreach":
            values = self.lookup_variable(loop["items"])
            require(isinstance(values, list) and len(values) <= loop.get("max_iterations", 10000), "Invalid foreach source")
            result = []
            for value in values:
                self.group_args[loop["item_name"]] = value
                for child in item["body"]:
                    events = [self.atomic(child)] if child["type"] == "node" else self.group(child, ancestors)
                    result.extend(e for e in events if e is not None)
            return result
        if loop["type"] == "count":
            count = loop.get("count")
            require(type(count) is int and 0 < count <= loop.get("max_iterations", 10000),
                    "Invalid count loop or insufficient iteration budget")
            require(not item.get("timeout_ms") and not loop.get("timeout_ms"),
                    "Count loop adds a timing constraint")
            require(bool(item.get("body")), "Empty count loop")
            return [event for _ in range(count) for child in item["body"] for event in
                    ([self.atomic(child)] if child["type"] == "node" else self.group(child, ancestors)) if event is not None]
        # Recognize a complete polling contract only after verifying its actual DSL body.
        require(loop["type"] == "do-while" and loop["condition"] == "${poll_ready} != true", "Polling condition differs")
        timeout_ms = self.resolve_arg(item["timeout_ms"])
        require(self.resolve_arg(loop["timeout_ms"]) == timeout_ms > 0, "Polling timeout differs")
        require(len(item["body"]) == 2, "Polling body differs")
        sample, delay = item["body"]
        node, action, bound, substitute = self.decode_node(sample)
        require(sample.get("result_key") == "poll_ready", "Polling result binding differs")
        expected = bytes.fromhex(substitute(action["response"]))
        require(expected[:2] == b"\x04\x02" and len(expected) == 4, "Polling response framing differs")
        require(node.get("judge") == {"type": "compare", "condition": "eq", "value": int.from_bytes(expected[2:], "big")},
                "Polling response predicate differs")
        require(action["parse"] == {"start_byte": 2, "length": 2, "endian": "big", "type": "uint16"}, "Polling decoder differs")
        require(delay.get("type") == "group" and delay.get("mode") == "if" and
                delay.get("condition") == {"type": "expression", "expression": "${poll_ready} != true"} and
                delay.get("else") == [] and len(delay.get("then", [])) == 1, "Polling wait guard differs")
        wait = self.atomic(delay["then"][0])
        require(wait["kind"] == "wait" and wait["ms"] > 0, "Polling interval differs")
        require(self.resolve_arg(loop["max_iterations"]) > timeout_ms // wait["ms"], "Iteration limit truncates timeout")
        failure = item["on_timeout"]
        require(len(failure) == 1 and failure[0]["type"] == "popup", "Polling timeout report absent")
        return [{"kind": "poll", "device": bound["slave_id"], "request": hexframe(substitute(action["request"])),
                 "expected_response": hexframe(substitute(action["response"])), "interval_ms": wait["ms"],
                 "timeout_ms": timeout_ms, "error_code": self.resolve_arg(failure[0]["error_code"]), "error_message": self.resolve_arg(failure[0]["message"])}]

    def events(self):
        require(self.flow.get("type") == "flow" and self.flow.get("mode") == "sequence", "Invalid L4")
        result = []
        for step, child in enumerate(self.flow["body"], 1):
            result.extend({"step": step, **event} for event in self.group(child))
        return result


def verify(directory):
    legacy = read(ROOT / "examples/beverage_legacy/beverage_filling_legacy.json")
    expected = []
    for step in legacy["steps"]:
        for seq in step["sequences"]:
            for group in seq["groups"]:
                expected.extend({"step": step["stepId"], **event} for event in legacy_events(group))
    reference_operations = len(expected)
    last_pr = {}
    retained = []
    omitted = []
    for index, event in enumerate(expected):
        if (event["kind"] == "write" and event["device"] in ("X", "Y1", "Y2", "Z")
                and event["request"].startswith("06 60 02 00 1")):
            point = int(event["request"].split()[-1], 16) - 16
            if last_pr.get(event["device"]) == point:
                omitted.append({"reference_index": index, **event})
                continue
            last_pr[event["device"]] = point
        retained.append(event)
    expected = retained
    layered = Layered(directory)
    actual = layered.events()
    require(len(expected) == len(actual), f"Operation count differs: {len(expected)} vs {len(actual)}")
    for i, (a, b) in enumerate(zip(expected, actual)):
        require(a == b, f"Operation {i} differs:\nlegacy={a}\nlayered={b}")
    require(set(layered.actions) == layered.used_actions, "Unused L1 definitions")
    require(set(layered.nodes) == layered.used_nodes, "Unused L2 definitions")
    require({p.resolve() for p in (directory / 'L3_group').glob('*.json')} == layered.used_groups, "Unreachable L3 definitions")
    counts = dict(Counter(e["kind"] for e in actual))
    files = sorted(directory.rglob("*.json"))
    normalized = [json.dumps(read(p), ensure_ascii=False, indent=4) + "\n" for p in files]
    size_by_component = {}
    for path, text in zip(files, normalized):
        component = path.relative_to(directory).parts[0]
        size = size_by_component.setdefault(component, {"files": 0, "lines": 0, "utf8_bytes": 0})
        size["files"] += 1
        size["lines"] += len(text.splitlines())
        size["utf8_bytes"] += len(text.encode())
    report = {"status": "PASS", "scope": "Static contracts match after omitting unchanged motor PR triggers as requested; not raw trace equality or hardware verification.",
              "source_sha256": hashlib.sha256((ROOT / "examples/beverage_legacy/beverage_filling_legacy.json").read_bytes()).hexdigest(),
              "reference_operations": reference_operations, "omitted_redundant_triggers": omitted,
              "steps": len(layered.flow["body"]), "operations": len(actual), "operation_kinds": counts,
              "internal_calculations": layered.calculations,
              "modbus_operations": sum(counts.get(k, 0) for k in ("write", "compare", "cache", "poll")),
              "definitions": {"L1": len(layered.actions), "L2": len(layered.nodes), "L3": len(layered.used_groups), "L4": 1},
              "group_calls": dict(layered.group_calls),
              "size_by_component": size_by_component,
              "normalized_size": {"files": len(files), "lines": sum(len(s.splitlines()) for s in normalized),
                                  "utf8_bytes": sum(len(s.encode()) for s in normalized)},
              "contracts": actual}
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--directory", type=Path, default=ROOT / "examples/example4")
    parser.add_argument("--report", type=Path)
    options = parser.parse_args()
    report = verify(options.directory)
    if options.report:
        options.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k not in ("contracts", "group_calls")}, indent=2))
