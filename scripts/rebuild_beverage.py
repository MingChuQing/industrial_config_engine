"""Translate the archived beverage configuration without dropping operations.

Only the six explicitly supported legacy leaf kinds are accepted. Register
addresses and frame layouts are fixed in L1; L2 adds execution/outcome policy.
L3 supplies operation arguments and composes reusable groups. Device PR setup
is completed separately; production configurations only call stored points.
"""
import argparse
from collections import Counter
import copy
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")


def slug(text):
    return re.sub(r"[^a-z0-9]+", "_", text.lower()).strip("_")


class Migration:
    def __init__(self):
        self.actions = {}
        self.nodes = {}
        self.node_keys = {}
        self.origins = []
        self.groups = {}
        self.group_keys = {}
        self.production_points = []
        self.axis_state = {"x": None, "y": None, "z": None}
        self.move_count = 0
        self.point_keys = {}

    def reusable(self, name, body, args=(), params=()):
        definition = {"type": "group", "name": name, "mode": "sequence", "body": body}
        if args:
            definition["args"] = [{"index": i, "name": n, "type": t}
                                  for i, (n, t) in enumerate(args)]
        key = canonical([body, args])
        if key not in self.group_keys:
            filename = slug(name) + ".group.json"
            assert filename not in self.groups
            self.groups[filename] = definition
            self.group_keys[key] = filename
        return {"type": "group", "template": "L3_group/" + self.group_keys[key], "params": list(params)}

    def action(self, name, kind, args, result="", arg_constraints=None, **fields):
        if kind.startswith("modbus_"):
            args = [(n, t) for n, t in args if n != "slave_id"]
        signature = name + ("." + "_".join(t for _, t in args) if args else "")
        if result:
            signature += ".r_" + result
        value = {"filename": signature, "type": kind,
                 "args": [{"index": i, "name": n, "type": t}
                          for i, (n, t) in enumerate(args)], **fields}
        for index, constraints in (arg_constraints or {}).items():
            value["args"][index].update(constraints)
        if signature in self.actions:
            assert self.actions[signature] == value
        self.actions[signature] = value
        return signature

    def node(self, label, action, params, result="b_done", **fields):
        device = None
        if self.actions[action]["type"].startswith("modbus_"):
            device, *params = params
            fields["device"] = "${device}"
        types = [a["type"] for a in self.actions[action]["args"]]
        payload = {"type": "node", "timeout_ms": 3000,
                   "max_retries": 0,
                   "action": {"template": "L1_action/all_actions.json/" + action}, **fields}
        key = canonical([payload, result])
        if key not in self.node_keys:
            filename = action.split(".", 1)[0]
            failure = fields.get("on_failure", [])
            if failure:
                filename += "_" + slug(failure[0]["error_code"])
            if types:
                filename += "." + "_".join(types)
            if result:
                filename += ".r_" + result
            assert filename not in self.nodes, "Conflicting semantic node name: " + filename
            self.nodes[filename] = {"filename": filename, "name": action.split(".", 1)[0], **payload}
            self.node_keys[key] = filename
        call = {"type": "node", "template": "L2_node/all_nodes.json/" + self.node_keys[key], "params": params}
        if device is not None:
            call["device"] = device
        return call

    def wait(self, ms):
        action = self.action("wait_ms", "wait", [("ms", "u32")], "b_wait_done", unit="ms")
        item = self.node("Wait milliseconds", action, [1000], timeout_ms=3600000)
        item["params"] = [ms]
        return item

    def read_action(self, source, polling=False):
        frame = bytes.fromhex(source["request"])
        assert len(frame) == 5 and frame[0] in (3, 4)
        if polling:
            parse = {"start_byte": 2, "length": 2, "endian": "big", "type": "uint16"}
            extra = {"response": source["expectedResponse"]}
        else:
            prefix = "cache" if source["type"] == "readAndCache" else "value"
            parse = {"start_byte": source[prefix + "StartByte"],
                     "length": source[prefix + "Length"], "endian": source[prefix + "Endian"],
                     "type": source["valueType"]}
            extra = {}
        result = "f_value" if parse["type"] == "float" else (
            ("i32_value" if parse["type"] == "int" else "u32_value")
            if parse["length"] == 4 else "u16_value")
        name = f"read_fc{frame[0]:02x}_reg{int.from_bytes(frame[1:3], 'big'):04x}"
        if parse["length"] == 1:
            name += "_byte"
        if parse["endian"] == "little":
            name += "_le"
        action = self.action(name, "modbus_read_cache", [("slave_id", "s")], result,
                             protocol="modbus", request=source["request"], parse=parse, **extra)
        return action, [source["device"]], result

    def leaf(self, source, origin):
        kind = source["type"]
        self.origins.append({"source": origin, "name": source["name"], "kind": kind})
        if kind == "timer":
            return self.wait(int(source["seconds"] * 1000))
        if kind == "updateUI":
            # UI values are display strings; ${cache} is interpolated at execution.
            action = self.action("update_ui", "ui_action", [("target", "s"), ("value", "s")],
                                 action="update_status", data={"target": "${target}", "value": "${value}"})
            return self.node(source["name"], action, [source["uiTarget"], source["uiValue"]], result="")
        if kind == "autorequestresponse":
            frame = bytes.fromhex(source["request"])
            assert len(frame) == 5 and frame[0] in (5, 6)
            assert bytes.fromhex(source["response"]) == frame
            # Register selection belongs to L1; values are operation arguments.
            request = frame[:3].hex(" ").upper() + " ${value_high} ${value_low}"
            address = int.from_bytes(frame[1:3], "big")
            if source["device"] in ("X", "Y1", "Y2", "Z") and frame[0] == 6 and 0x6200 <= address < 0x6280:
                raise ValueError("PR point programming belongs to device commissioning, not production")
            label = {0x6002: "trigger_pr_path", 0x1801: "set_motor_enable",
                     0x600A: "set_homing_mode"}.get(address, "write_" + frame[:3].hex())
            action = self.action(label, "modbus_write_verify",
                                 [("slave_id", "s"), ("value", "u16")], "b_written",
                                 protocol="modbus", request=request, response=request)
            return self.node(source["name"], action, [source["device"], int.from_bytes(frame[3:5], "big")],
                             **({"judge": {"type": "exists"}} if address == 0x6002 else {}))
        if kind not in ("readAndCompare", "readAndCache", "loopUntilResponse"):
            raise ValueError(f"Unsupported legacy operation {kind}: {origin}")
        action, params, result = self.read_action(source, kind == "loopUntilResponse")
        if kind == "readAndCache":
            item = self.node(source["name"], action, params, result=result)
            item["result_key"] = source["cacheKey"]
            return item
        if kind == "readAndCompare":
            condition = {"equal": "eq", "greaterOrEqual": "ge"}[source["compareType"]]
            return self.node(source["name"], action, params, result="b_check_ok",
                             judge={"type": "compare", "condition": condition, "value": source["compareValue"]},
                             on_failure=[{"type": "popup", "level": "error", "message": source["errorMessage"],
                                          "error_code": source["errorCode"]}])
        expected = bytes.fromhex(source["expectedResponse"])
        assert expected[:2] == b"\x04\x02" and len(expected) == 4
        sensor = self.node(source["name"], action, params, result="b_poll_ready",
                           judge={"type": "compare", "condition": "eq", "value": int.from_bytes(expected[2:], "big")})
        read_call = {"type": "node", "template": "${poll_node}", "params": [],
                     "device": "${poll_device}", "result_key": "poll_ready"}
        wait_if = {"type": "group", "name": "Wait only when response has not matched", "mode": "if",
                   "condition": {"type": "expression", "expression": "${poll_ready} != true"},
                   "then": [self.wait("${poll_interval_ms}")], "else": []}
        filename = "poll_sensor_until_ready.group.json"
        self.groups[filename] = {
            "type": "group", "name": "Poll sensor until ready", "mode": "loop",
            "args": [{"index": i, "name": name, "type": typ}
                     for i, (name, typ) in enumerate([
                         ("poll_node", "s"), ("poll_device", "s"), ("poll_interval_ms", "u32"),
                         ("poll_timeout_ms", "u32"), ("poll_max_iterations", "u32"),
                         ("poll_error_code", "s"), ("poll_error_message", "s")])],
            "timeout_ms": "${poll_timeout_ms}",
            "loop": {"type": "do-while", "condition": "${poll_ready} != true",
                     "timeout_ms": "${poll_timeout_ms}", "max_iterations": "${poll_max_iterations}"},
            "body": [read_call, wait_if],
            "on_timeout": [{"type": "popup", "level": "error", "message": "${poll_error_message}",
                            "error_code": "${poll_error_code}"}]}
        return {"type": "group", "name": source["name"], "template": "L3_group/" + filename,
                "params": [sensor["template"], source["device"], source["intervalMs"], source["timeoutMs"],
                           source["timeoutMs"] // source["intervalMs"] + 2, source["errorCode"], source["timeoutMsg"]]}

    def source_leaves(self, obj):
        if "actions" in obj:
            return [leaf for child in obj["actions"] for leaf in self.source_leaves(child)]
        return [obj]

    def flatten(self, obj, origin):
        leaves = self.source_leaves(obj)
        output = []
        i = 0
        while i < len(leaves):
            leaf = leaves[i]
            is_trigger = (leaf.get("type") == "autorequestresponse"
                          and leaf.get("device") in ("X", "Y1", "Z")
                          and leaf["request"].startswith("06 60 02 00 1"))
            if not is_trigger:
                output.append(self.leaf(leaf, origin + f"/{i}"))
                i += 1
                continue
            paired = leaf["device"] == "Y1"
            triggers = 2 if paired else 1
            if paired:
                assert leaves[i + 1]["device"] == "Y2"
                assert leaves[i + 1]["request"] == leaf["request"]
            wait = leaves[i + triggers]
            refresh = leaves[i + triggers + 1:i + triggers + 8]
            assert wait["type"] == "timer" and len(refresh) == 7
            assert [x["type"] for x in refresh] == ["readAndCache"] * 4 + ["updateUI"] * 3
            refresh_body = [self.leaf(x, origin + "/refresh") for x in refresh]
            refresh_call = self.reusable("Refresh all motor positions and UI", refresh_body)
            encode = self.action("encode_pr_trigger", "calculate", [("point", "u16")], "u16_motor_trigger_word",
                                 expression="16 + ${param_0}", arg_constraints={0: {"min": 0, "max": 15}})
            encode_call = self.node("Encode PR trigger", encode, ["${motor_pr}"], result="u16_motor_trigger_word")
            encode_call["result_key"] = "motor_trigger_word"
            trigger = copy.deepcopy(leaf)
            trigger["device"] = "${motor_device}"
            trigger_call = self.leaf(trigger, origin + "/trigger")
            trigger_call["params"] = ["${motor_trigger_word}"]
            trigger_call["result_key"] = "motor_trigger_ok"
            trigger_group = self.reusable("Trigger motor PR", [encode_call, trigger_call],
                                          [("motor_device", "s"), ("motor_pr", "u16")],
                                          ["${active_motor}", "${axis_pr}"])
            state_action = self.action("remember_pr_state", "set_variable", [("key", "s"), ("value", "i32")], "b_saved")
            def remember(key, value):
                return self.node("Remember PR state", state_action, [key, value])
            read_state = self.action("read_pr_state", "read_variable", [("key", "s")], "i32_previous_pr")
            read_call = self.node("Read PR state", read_state, ["${axis_state_key}"], result="i32_previous_axis_pr")
            read_call["result_key"] = "previous_axis_pr"
            def conditional(name, expression, body):
                return {"type": "group", "name": name, "mode": "if",
                        "condition": {"type": "expression", "expression": expression}, "then": body, "else": []}
            motor_loop = {"type": "group", "name": "Trigger axis motors", "mode": "loop",
                          "loop": {"type": "foreach", "items": "axis_devices", "item_name": "active_motor"},
                          "body": [trigger_group, conditional("Record rejected command", "${motor_trigger_ok} != true",
                                    [remember("axis_failed", 1)])]}
            changed = conditional("Only move changed axis", "${axis_pr} != ${previous_axis_pr}",
                       [remember("${axis_state_key}", -1), remember("axis_failed", 0), motor_loop,
                        conditional("Remember successfully issued point", "${axis_failed} == 0",
                                    [remember("${axis_state_key}", "${axis_pr}")])])
            axis_args = [("axis_state_key", "s"), ("axis_pr", "u16"), ("axis_devices", "arr")]
            axis_body = [read_call, changed]
            devices = {"x": ["X"], "y": ["Y1", "Y2"], "z": ["Z"]}
            axis = {"X": "x", "Y1": "y", "Z": "z"}[leaf["device"]]
            pr = int(leaf["request"].split()[-1], 16) - 16
            self.axis_state[axis] = pr
            self.move_count += 1
            if self.move_count <= 3:
                assert pr == 0 and axis == "xyz"[self.move_count - 1]
                axis_call = self.reusable("Move axis if changed", axis_body, axis_args,
                                          ["${home_state_key}", 0, "${home_devices}"])
                output.append(self.reusable("Home axis and refresh",
                              [axis_call, self.wait(3000), refresh_call],
                              [("home_state_key", "s"), ("home_devices", "arr")],
                              ["last_" + axis + "_pr", devices[axis]]))
            else:
                assert all(value is not None for value in self.axis_state.values())
                key = canonical([self.axis_state, wait["seconds"]])
                if key not in self.point_keys:
                    self.point_keys[key] = len(self.production_points)
                    self.production_points.append({"name": obj["name"], **self.axis_state,
                                                   "wait_ms": int(wait["seconds"] * 1000)})
                dispatch = [self.reusable("Move axis if changed", axis_body, axis_args,
                            ["last_" + a + "_pr", "${production_points[point_index]." + a + "}", devices[a]])
                            for a in "xyz"]
                wait_call = self.wait("${production_points[point_index].wait_ms}")
                output.append(self.reusable("Move production point", dispatch + [wait_call, refresh_call],
                                            [("point_index", "u16")], [self.point_keys[key]]))
            i += triggers + 8
        return output

    def build(self, legacy, output):
        assert legacy["mode"] == 3 and all(s["mountMode"] == 3 for s in legacy["steps"])
        chunks = []
        stages = []
        for si, step in enumerate(legacy["steps"]):
            stage = []
            for qi, seq in enumerate(step["sequences"]):
                for gi, group in enumerate(seq["groups"]):
                    body = self.flatten(group, f"/steps/{si}/sequences/{qi}/groups/{gi}")
                    chunk = (group["name"], body)
                    stage.append(chunk)
                    chunks.append(chunk)
            stages.append((step, stage))
        repeated = Counter(canonical(body) for _, body in chunks if len(body) > 1)
        helpers = {}
        flow = {"type": "flow", "name": "Beverage Filling main flow", "mode": "sequence", "body": []}
        for step, stage in stages:
            body = []
            for name, items in stage:
                key = canonical(items)
                if len(items) > 1 and repeated[key] > 1:
                    if key not in helpers:
                        filename = "shared_" + slug(name) + ".group.json"
                        assert filename not in helpers.values(), "Conflicting shared group name"
                        helpers[key] = filename
                        save(output / "L3_group" / filename,
                             {"type": "group", "name": name, "mode": "sequence", "body": items})
                    body.append({"type": "group", "template": "L3_group/" + helpers[key], "params": []})
                else:
                    body.extend(items)
            # Consecutive identical calls remain repeated operations, represented
            # once in a count-loop body. Their position in the sequence is fixed.
            compact = []
            i = 0
            while i < len(body):
                j = i + 1
                while j < len(body) and body[i].get("type") == "node" and body[j] == body[i]:
                    j += 1
                if j - i >= 3:
                    compact.append({"type": "group", "name": "Repeat consecutive checks",
                                    "mode": "loop", "loop": {"type": "count", "count": j - i},
                                    "body": [body[i]]})
                else:
                    compact.extend(body[i:j])
                i = j
            body = compact
            filename = f"step_{step['stepId']:02d}_" + slug(step["task"]) + ".group.json"
            save(output / "L3_group" / filename,
                 {"type": "group", "name": step["task"], "mode": "sequence", "max_nodes": 200, "body": body})
            flow["body"].append({"type": "group", "name": f"Step {step['stepId']}: {step['task']}",
                                 "template": "L3_group/" + filename, "params": []})
        self.groups["move_production_point.group.json"]["args"][0].update({"min": 0, "max": 100})
        save(output / "system_variables.json", {"production_points": self.production_points, "last_x_pr": -1, "last_y_pr": -1, "last_z_pr": -1})
        save(output / "device_registry.json", json.loads(
            (ROOT / "examples/example4/device_registry.json").read_text(encoding="utf-8-sig")))
        save(output / "L1_action/all_actions.json", {"type": "action_bundle", "actions": list(self.actions.values())})
        save(output / "L2_node/all_nodes.json", {"type": "node_bundle", "nodes": list(self.nodes.values())})
        save(output / "L4_flow/main_flow.json", flow)
        for filename, definition in self.groups.items():
            save(output / "L3_group" / filename, definition)
        return {"actions": len(self.actions), "nodes": len(self.nodes), "groups": len(stages) + len(helpers) + len(self.groups),
                "flows": 1, "production_points": len(self.production_points)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True, help="Empty candidate output directory")
    args = parser.parse_args()
    if args.output.exists() and any(args.output.iterdir()):
        parser.error("Output must be empty; verify the candidate before replacing example4")
    source = json.loads((ROOT / "examples/beverage_legacy/beverage_filling_legacy.json").read_text(encoding="utf-8-sig"))
    print(json.dumps(Migration().build(source, args.output), indent=2))
