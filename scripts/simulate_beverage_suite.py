"""Independent interpreters sharing a deterministic virtual production line.

Interpreters execute the saved monolithic JSON and the saved L1-L4 files.
The generator is not imported. Time is virtual; no physical I/O is performed.
"""
import argparse
import copy
import hashlib
import json
import math
from pathlib import Path
import re
import struct

ROOT = Path(__file__).resolve().parents[1]


class SimulationError(Exception):
    def __init__(self, code, message):
        self.code, self.message = code, message
        super().__init__(code + ": " + message)


def read(path):
    return json.loads(Path(path).read_text(encoding="utf-8-sig"))


def text(value):
    if isinstance(value, bool): return str(value).lower()
    if isinstance(value, float) and value.is_integer(): return str(int(value))
    return str(value)


def decode_number(response, start, length, endian, value_type):
    """Decode the numeric payload specified by a saved configuration."""
    payload = response[start:start + length]
    if len(payload) != length or endian not in ("big", "little"):
        raise ValueError("Invalid numeric response layout")
    if value_type == "float":
        if length != 4: raise ValueError("Float response must contain four bytes")
        return struct.unpack((">" if endian == "big" else "<") + "f", payload)[0]
    return int.from_bytes(payload, endian, signed=value_type == "int")


def compare_number(value, condition, expected):
    if condition in ("eq", "equal"): return value == expected
    if condition in ("ge", "greaterOrEqual"): return value >= expected
    raise ValueError("Unsupported numeric comparison: " + condition)


class Variables:
    def __init__(self, values=None): self.values = copy.deepcopy(values or {})
    def get(self, key):
        if key in self.values: return self.values[key]
        match = re.match(r"^[^.\[]+", key)
        if not match or match[0] not in self.values: raise SimulationError("VARIABLE", key)
        value, tail = self.values[match[0]], key[len(match[0]):]
        while tail:
            part = re.match(r"^\.([^.\[]+)|^\[([^\]]+)\]", tail)
            if not part: raise SimulationError("VARIABLE_PATH", key)
            if part[1] is not None: value = value[part[1]]
            else:
                index = int(part[2]) if part[2].isdigit() else self.values[part[2]]
                if type(index) is not int or not 0 <= index < len(value): raise SimulationError("INDEX", key)
                value = value[index]
            tail = tail[len(part[0]):]
        return value
    def resolve(self, value):
        if isinstance(value, list): return [self.resolve(x) for x in value]
        if not isinstance(value, str): return value
        exact = re.fullmatch(r"\$\{([^}]+)\}", value)
        if exact: return copy.deepcopy(self.get(exact[1]))
        return re.sub(r"\$\{([^}]+)\}", lambda m: text(self.get(m[1])), value)


class VirtualLine:
    MOTORS = ("X", "Y1", "Y2", "Z", "Infeed", "Closure", "Outfeed")
    FILL_X = {"OrangeFill": 4, "AppleFill": 5, "GrapeFill": 6}
    def __init__(self, registry, recipe, fault=None):
        self.devices, self.recipe, self.fault = registry["devices"], recipe, fault
        self.now, self.trace, self.pending = 0, [], []
        self.registers = {d: {0: 1, 1: 0, 0x602C: 0} for d in self.devices}
        self.coils = {d: False for d in self.devices}
        self.motor_pr = {d: 0 for d in self.MOTORS}
        self.empty_ready = self.held = self.filled = self.closed = self.output_pending = False
        self.delivered = []
        self.dispensed_ml = 0
        self.validate_registry()
    def validate_registry(self):
        endpoints = []
        for d, spec in self.devices.items():
            if not isinstance(spec.get("port"), str) or not spec["port"] or not 1 <= spec["address"] <= 247:
                raise ValueError("Invalid endpoint: " + d)
            endpoints.append((spec["port"], spec["address"]))
        if len(set(endpoints)) != len(endpoints): raise ValueError("Duplicate endpoint")
    def record(self, kind, **fields): self.trace.append({"time_ms": self.now, "kind": kind, **fields})
    def schedule(self, delay, device, kind, value=0):
        self.pending.append((self.now + delay, len(self.pending), device, kind, value))
    def advance(self, ms):
        target = self.now + ms
        while self.pending and min(x[0] for x in self.pending) <= target:
            item = min(self.pending, key=lambda x: (x[0], x[1]))
            self.pending.remove(item)
            self.now, _, device, kind, value = item
            self.registers[device][0] = 1
            if kind == "motor":
                self.motor_pr[device] = value
                self.registers[device][0x602C] = value * 1000
                if device == "Infeed": self.empty_ready = True
                if device == "Closure": self.closed = True
                if device == "Outfeed":
                    self.output_pending = False
                    self.delivered.append({"flavor": self.recipe["flavor"], "format": self.recipe["format"],
                                           "volume_ml": self.dispensed_ml, "closure": self.recipe["closure_mode"]})
                    self.record("finished_container", number=len(self.delivered), **self.delivered[-1])
            elif kind == "fill":
                self.registers[device][0x0012] = value
                self.dispensed_ml, self.filled = value, True
            self.record("device_complete", device=device, operation=kind, value=value)
        self.now = target
    def wait(self, ms):
        self.record("wait", duration_ms=ms)
        self.advance(ms)
    def ui(self, target, value):
        self.record("ui", target=target, value=text(value))
        self.advance(10)
    def fail(self, code, message):
        self.record("error", code=code, message=message)
        raise SimulationError(code, message)
    def write_register(self, device, address, value):
        self.registers[device][address] = value
        if address != 0x6002: return
        point = value - 16
        if device not in self.MOTORS or not 0 <= point <= 15: self.fail("PR_RANGE", device)
        if device == "Closure" and not (self.held and self.filled and self.coils["CapFeeder"]):
            self.fail("CLOSURE_SEQUENCE", "Container must be filled and closure feeder active")
        if device == "Closure" and point != self.recipe["closure_pr"]: self.fail("CLOSURE_MODE", "Wrong closure mechanism")
        if device == "Outfeed" and not self.output_pending: self.fail("OUTFEED_SEQUENCE", "No released finished container")
        self.registers[device][0] = 0
        if self.fault == "infeed_jam" and device == "Infeed": return
        if self.fault == "closure_jam" and device == "Closure": return
        duration = 30 + 5 * abs(point - self.motor_pr[device])
        self.schedule(duration, device, "motor", point)
    def write_coil(self, device, on):
        self.coils[device] = on
        if device == "Gripper":
            if on:
                if not self.empty_ready or self.held: self.fail("PICK_SEQUENCE", "No available empty container")
                if self.motor_pr["X"] not in (1, 2, 3): self.fail("PICK_POSITION", "Not at empty-container station")
                self.empty_ready, self.held, self.filled, self.closed = False, True, False, False
                self.registers["ContainerSensor"][1] = 1
            elif self.held:
                if not self.filled or not self.closed: self.fail("UNFINISHED_RELEASE", "Container is not filled and closed")
                if self.motor_pr["X"] not in (10, 11, 12): self.fail("RELEASE_POSITION", "Not at finished station")
                self.held, self.output_pending = False, True
                self.registers["ContainerSensor"][1] = 0
        elif device in self.FILL_X and on:
            if not self.held or self.motor_pr["X"] != self.FILL_X[device]: self.fail("FILL_POSITION", "Wrong fill station")
            if device != self.recipe["fill_device"]: self.fail("WRONG_FLAVOR", device)
            volume = self.registers[device].get(0x0010, 0)
            if volume != self.recipe["volume_ml"]: self.fail("WRONG_VOLUME", str(volume))
            self.registers[device][0] = 0
            self.registers[device][0x0012] = 0
            duration = math.ceil(volume * 1000 / self.recipe["fill_rate_ml_s"])
            if 'fill_time_ms' in self.recipe:
                duration = self.registers[device].get(0x0011, 0)
                if duration != self.recipe['fill_time_ms']:
                    self.fail('WRONG_FILL_TIME', str(duration))
            self.schedule(duration, device, "fill", volume)
    def transact(self, device, pdu):
        if device not in self.devices: self.fail("DEVICE", device)
        data = bytes.fromhex(pdu)
        if len(data) != 5 or data[0] not in (3, 4, 5, 6): self.fail("PDU", pdu)
        fc, address, value = data[0], int.from_bytes(data[1:3], "big"), int.from_bytes(data[3:5], "big")
        spec = self.devices[device]
        if self.fault == "fill_offline" and device == self.recipe["fill_device"] and self.held:
            self.record("modbus", device=device, port=spec["port"], address=spec["address"], request=pdu, response=None)
            self.fail("COMM_OFFLINE", device)
        if fc == 6: self.write_register(device, address, value); response = data
        elif fc == 5: self.write_coil(device, value == 0xFF00); response = data
        else:
            if value not in (1, 2): self.fail("READ_COUNT", str(value))
            number = self.registers[device].get(address, 0)
            response = bytes([fc, value * 2]) + number.to_bytes(value * 2, "big", signed=number < 0)
        reply = response.hex(" ").upper()
        self.record("modbus", device=device, port=spec["port"], address=spec["address"], request=pdu, response=reply)
        self.advance(5)
        return response


class LegacyInterpreter:
    def __init__(self, line): self.line, self.vars = line, Variables()
    def run(self, document): self.execute(document["actions"])
    def execute(self, actions):
        for action in actions:
            kind = action["type"]
            if kind == "repeat":
                for _ in range(action["count"]): self.execute(action["actions"])
            elif kind == "ifVariableDiffers":
                if self.vars.get(action["variable"]) != action["value"]: self.execute(action["actions"])
            elif kind == "setVariable": self.vars.values[action["key"]] = action["value"]
            elif kind == "incrementVariable": self.vars.values[action["key"]] += action["amount"]
            elif kind == "autorequestresponse":
                result = self.line.transact(action["device"], action["request"])
                if result != bytes.fromhex(action["response"]): self.line.fail("WRITE_ECHO", action["name"])
            elif kind == "readAndCache":
                response = self.line.transact(action["device"], action["request"])
                a, n = action["cacheStartByte"], action["cacheLength"]
                value = decode_number(response, a, n, action["cacheEndian"], action["valueType"])
                self.vars.values[action["cacheKey"]] = value
                self.line.record("cache", key=action["cacheKey"], value=value)
            elif kind == "readAndCompare":
                response = self.line.transact(action["device"], action["request"])
                value = decode_number(response, action["valueStartByte"], action["valueLength"],
                                      action["valueEndian"], action["valueType"])
                if not compare_number(value, action["compareType"], action["compareValue"]):
                    self.line.fail(action["errorCode"], action["errorMessage"])
            elif kind == "updateUI": self.line.ui(action["uiTarget"], self.vars.resolve(action["uiValue"]))
            elif kind == "loopUntilResponse":
                start, iteration = self.line.now, 0
                while True:
                    if self.line.now - start >= action["timeoutMs"] or iteration >= action.get("maxIterations", action["timeoutMs"] // action["intervalMs"] + 2):
                        self.line.fail(action["errorCode"], action["timeoutMsg"])
                    iteration += 1
                    response = self.line.transact(action["device"], action["request"])
                    if response == bytes.fromhex(action["expectedResponse"]): break
                    self.line.wait(action["intervalMs"])
            elif kind == "timer": self.line.wait(int(action["seconds"] * 1000))
            else: raise ValueError("Unknown legacy action: " + kind)


class LayeredInterpreter:
    def __init__(self, root, line, variables):
        self.root, self.line, self.vars = Path(root), line, Variables(variables)
        self.actions = {x["filename"]: x for x in read(self.root / "L1_action/all_actions.json")["actions"]}
        self.nodes = {x["filename"]: x for x in read(self.root / "L2_node/all_nodes.json")["nodes"]}
        self.used_actions, self.used_nodes, self.used_groups = set(), set(), set()
        self.group_calls = {}
    def run(self, flow): self.execute(flow["body"])
    def condition(self, config):
        match = re.fullmatch(r"(\$\{[^}]+\}) (!=|==) (\$\{[^}]+\}|true|false|-?\d+)", config["expression"])
        if not match: raise ValueError("Unsupported condition")
        left = self.vars.resolve(match[1])
        right = self.vars.resolve(match[3]) if match[3].startswith("${") else json.loads(match[3])
        return left != right if match[2] == "!=" else left == right
    def execute(self, body):
        for item in body:
            kind = item["type"]
            if kind == "node": self.node(item)
            elif kind == "group": self.group(item)
            elif kind == "popup": self.line.fail(self.vars.resolve(item["error_code"]), self.vars.resolve(item["message"]))
            else: raise ValueError("Unsupported body item: " + kind)
    def group(self, call):
        group = call
        if "template" in call:
            reference = call["template"]
            path = (self.root / reference).resolve()
            if not path.is_relative_to((self.root / "L3_group").resolve()): raise ValueError("Invalid group reference")
            group = read(path)
            self.used_groups.add(reference)
            self.group_calls[reference] = self.group_calls.get(reference, 0) + 1
            values = self.vars.resolve(call.get("params", []))
            if len(values) != len(group.get("args", [])): raise ValueError("Group argument count")
            for arg, value in zip(group.get("args", []), values):
                if "min" in arg and (type(value) is not int or not arg["min"] <= value <= arg["max"]):
                    raise ValueError("Group argument bounds")
                self.vars.values[arg["name"]] = value
        mode = group["mode"]
        if mode == "sequence": self.execute(group["body"])
        elif mode == "if": self.execute(group["then"] if self.condition(group["condition"]) else group["else"])
        elif mode == "loop":
            loop = group["loop"]
            if loop["type"] == "count":
                for _ in range(self.vars.resolve(loop["count"])): self.execute(group["body"])
            elif loop["type"] == "foreach":
                for value in copy.deepcopy(self.vars.get(loop["items"])):
                    self.vars.values[loop["item_name"]] = value
                    self.execute(group["body"])
            elif loop["type"] == "do-while":
                start, iteration = self.line.now, 0
                deadline, limit = self.vars.resolve(loop["timeout_ms"]), self.vars.resolve(loop["max_iterations"])
                while True:
                    if self.line.now - start >= deadline or iteration >= limit:
                        self.execute(group["on_timeout"])
                        raise ValueError("Missing timeout failure")
                    iteration += 1
                    self.execute(group["body"])
                    if not self.condition({"expression": loop["condition"]}): break
            else: raise ValueError("Unsupported loop")
        else: raise ValueError("Unsupported group mode")
    def node(self, item):
        reference = self.vars.resolve(item["template"])
        if not reference.startswith("L2_node/all_nodes.json/"): raise ValueError("Invalid node reference")
        name = reference.rsplit("/", 1)[1]
        node = self.nodes[name]
        action_ref = node["action"]["template"]
        if not action_ref.startswith("L1_action/all_actions.json/"): raise ValueError("Invalid action reference")
        action = self.actions[action_ref.rsplit("/", 1)[1]]
        self.used_nodes.add(name); self.used_actions.add(action["filename"])
        params = self.vars.resolve(item.get("params", []))
        if len(params) != len(action["args"]): raise ValueError("Action argument count")
        bound = {}
        for i, (arg, value) in enumerate(zip(action["args"], params)):
            if "min" in arg and (type(value) is not int or not arg["min"] <= value <= arg["max"]): raise ValueError("PR bounds")
            bound[arg["name"]] = value; self.vars.values["param_" + str(i)] = value
        def frame(template):
            def sub(match):
                name = match[1]
                if name.endswith("_high") or name.endswith("_low"):
                    key, part = name.rsplit("_", 1); number = bound[key]
                    return f"{(number >> 8 if part == 'high' else number) & 255:02X}"
                raise ValueError("Unknown PDU placeholder")
            return re.sub(r"\$\{([^}]+)\}", sub, template)
        kind, result = action["type"], None
        if kind.startswith("modbus_"):
            device = self.vars.resolve(item.get("device", node.get("device")))
            response = self.line.transact(device, frame(action["request"]))
            if kind == "modbus_write_verify":
                if response != bytes.fromhex(frame(action["response"])): self.line.fail("WRITE_ECHO", name)
                result = True
            else:
                parse = action["parse"]; start = parse["start_byte"]
                result = decode_number(response, start, parse["length"], parse["endian"], parse["type"])
                if "result_key" in item and "judge" not in node:
                    self.line.record("cache", key=item["result_key"], value=result)
        elif kind == "calculate":
            expression = self.vars.resolve(action["expression"])
            if not re.fullmatch(r"-?\d+ \+ -?\d+", expression): raise ValueError("Arithmetic subset")
            result = sum(int(x.strip()) for x in expression.split("+"))
        elif kind == "set_variable": self.vars.values[params[0]] = params[1]; result = True
        elif kind == "read_variable": result = self.vars.get(params[0])
        elif kind == "wait": self.line.wait(params[0]); result = True
        elif kind == "ui_action": self.line.ui(params[0], params[1]); result = True
        else: raise ValueError("Unsupported action " + kind)
        success = True
        if "judge" in node:
            judge = node["judge"]
            if judge["type"] == "compare":
                success = compare_number(result, judge["condition"], self.vars.resolve(judge["value"]))
            elif judge["type"] == "exists": success = result is not None
            else: raise ValueError("Judge subset")
        for definition, value in [(action, result), (node, success if ".r_b_" in name else result)]:
            if ".r_" in definition["filename"]:
                sig = definition["filename"].split(".r_", 1)[1]
                if "_" in sig: self.vars.values[sig.split("_", 1)[1]] = value
        if "result_key" in item: self.vars.values[item["result_key"]] = success if ".r_b_" in name else result
        if not success and "on_failure" in node: self.execute(node["on_failure"])


def run_pair(root, recipe_index, fault=None, trace_dir=None):
    root = Path(root); variables = read(root / "system_variables.json")
    recipe = variables["recipes"][recipe_index]; registry = read(root / "device_registry.json")
    legacy, flow = read(root / "legacy" / (recipe["id"] + ".json")), read(root / "layered/L4_flow" / (recipe["id"] + ".json"))
    runs = []
    for representation, doc in [("legacy", legacy), ("layered", flow)]:
        line = VirtualLine(registry, recipe, fault)
        interpreter = LegacyInterpreter(line) if representation == "legacy" else LayeredInterpreter(root / "layered", line, variables)
        status = "SUCCESS"
        try: interpreter.run(doc)
        except SimulationError as error: status = error.code
        if trace_dir:
            trace_dir.mkdir(parents=True, exist_ok=True)
            with (trace_dir / f"{recipe['id']}_{representation}.jsonl").open("w", encoding="utf-8") as stream:
                for event in line.trace: stream.write(json.dumps(event, ensure_ascii=False, separators=(",", ":")) + "\n")
        runs.append((line, interpreter, status))
    left, right = runs[0][0], runs[1][0]
    if left.trace != right.trace:
        mismatch = next((i for i, (a, b) in enumerate(zip(left.trace, right.trace)) if a != b), min(len(left.trace), len(right.trace)))
        raise AssertionError(f"{recipe['id']}: trace mismatch at {mismatch}: {left.trace[mismatch:mismatch+1]} != {right.trace[mismatch:mismatch+1]}")
    assert runs[0][2] == runs[1][2] and left.delivered == right.delivered
    for key in ["completed", "last_x_pr", "last_y_pr", "last_z_pr"]:
        assert runs[0][1].vars.get(key) == runs[1][1].vars.get(key), key
    if fault is None:
        assert len(left.delivered) == recipe["batches"] * recipe["units_per_batch"] == runs[1][1].vars.get("completed")
        assert sum(p["volume_ml"] for p in left.delivered) == 12 * recipe["volume_ml"]
        assert not left.pending and not left.held and not any(left.coils.values())
    serialized = json.dumps(left.trace, ensure_ascii=False, separators=(",", ":"))
    return {"recipe": recipe["id"], "fault": fault, "status": runs[0][2], "trace_equal": True,
            "events": len(left.trace), "modbus_transactions": sum(e["kind"] == "modbus" for e in left.trace),
            "simulated_ms": left.now, "finished_containers": len(left.delivered),
            "filled_total_ml": sum(p["volume_ml"] for p in left.delivered),
            "trace_sha256": hashlib.sha256(serialized.encode()).hexdigest(),
            "group_calls": runs[1][1].group_calls,
            "used_actions": sorted(runs[1][1].used_actions), "used_nodes": sorted(runs[1][1].used_nodes),
            "used_groups": sorted(runs[1][1].used_groups)}


def size(paths):
    texts = [json.dumps(read(p), ensure_ascii=False, indent=4) + "\n" for p in paths]
    return {"files": len(texts), "lines": sum(len(s.splitlines()) for s in texts), "utf8_bytes": sum(len(s.encode()) for s in texts)}


def compare(root, trace_dir=None):
    root = Path(root)
    variables = read(root / "system_variables.json")
    results = [run_pair(root, i, trace_dir=trace_dir) for i in range(9)]
    faults = [run_pair(root, 0, fault) for fault in ("infeed_jam", "fill_offline", "closure_jam")]
    assert [r["status"] for r in faults] == ["EMPTY_FEED_TIMEOUT", "COMM_OFFLINE", "CLOSURE_TIMEOUT"]
    actions = read(root / "layered/L1_action/all_actions.json")["actions"]
    nodes = read(root / "layered/L2_node/all_nodes.json")["nodes"]
    assert set(a["filename"] for a in actions) == set().union(*(set(r["used_actions"]) for r in results))
    assert set(n["filename"] for n in nodes) == set().union(*(set(r["used_nodes"]) for r in results))
    assert {"L3_group/" + p.name for p in (root / "layered/L3_group").glob("*.json")} == set().union(*(set(r["used_groups"]) for r in results))
    sizes = {"legacy": size(sorted((root / "legacy").glob("*.json"))),
             "L1": size(sorted((root / "layered/L1_action").glob("*.json"))),
             "L2": size(sorted((root / "layered/L2_node").glob("*.json"))),
             "L3": size(sorted((root / "layered/L3_group").glob("*.json"))),
             "L4": size(sorted((root / "layered/L4_flow").glob("*.json"))),
             "devices": size([root / "device_registry.json"]), "points_and_recipes": size([root / "system_variables.json"])}
    assert sizes["legacy"]["files"] == sizes["L4"]["files"] == 9 and sizes["legacy"]["lines"] > 10000
    per_recipe = []
    for recipe in variables["recipes"]:
        per_recipe.append({"recipe": recipe["id"], "legacy": size([root / "legacy" / (recipe["id"] + ".json")]),
                           "L4": size([root / "layered/L4_flow" / (recipe["id"] + ".json")])})
    for result in results + faults:
        for key in ("used_actions", "used_nodes", "used_groups"): result.pop(key)
    return {"status": "PASS", "provenance": "Constructed synthetic benchmark; not measured industrial deployment data.",
            "scope": "Two independently implemented interpreters, same deterministic virtual hardware model; exact timed observable trace equality.",
            "sizes": sizes, "per_recipe_sizes": per_recipe, "recipes": results, "fault_cases": faults}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "examples/beverage_suite")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--traces", type=Path)
    args = parser.parse_args()
    report = compare(args.root, args.traces)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "sizes": report["sizes"],
                      "recipes": [{k: v for k, v in r.items() if k not in ("group_calls", "trace_sha256")} for r in report["recipes"]],
                      "fault_cases": [{"fault": r["fault"], "status": r["status"], "trace_equal": r["trace_equal"]} for r in report["fault_cases"]]}, indent=2))
