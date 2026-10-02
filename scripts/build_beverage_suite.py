"""Generate an explicitly synthetic nine-recipe benchmark, without padded JSON.

The same recipe specification is rendered independently as expanded monolithic
control logic and as reusable L1-L4 templates. This is a constructed example,
not an archival industrial dataset or an automatic optimization benchmark.
"""
import argparse
import copy
import json
from pathlib import Path
from rebuild_beverage import Migration, save

ROOT = Path(__file__).resolve().parents[1]
MOTORS = ["X", "Y1", "Y2", "Z", "Infeed", "Closure", "Outfeed"]
FLAVORS = [("orange", "OrangeFill", 200), ("apple", "AppleFill", 180), ("grape", "GrapeFill", 220)]
FORMATS = [("family", 2000, "screw_large"), ("can", 330, "seam_can"), ("small", 250, "screw_small")]


def specification():
    points, lookup, recipes = [], {}, []
    def point(x, y, z, name):
        key = (x, y, z)
        if key not in lookup:
            lookup[key] = len(points)
            points.append({"name": name, "x": x, "y": y, "z": z})
        return lookup[key]
    home = point(0, 0, 1, "Home travel height")
    for fi, (flavor, head, rate) in enumerate(FLAVORS):
        for si, (size, volume, closure) in enumerate(FORMATS):
            recipe = {"id": flavor + "_" + size, "flavor": flavor, "format": size,
                      "volume_ml": volume, "fill_device": head, "fill_rate_ml_s": rate,
                      "closure_mode": closure, "closure_pr": si + 1,
                      "infeed_pr": si + 1, "outfeed_pr": si + 4, "home_point": home,
                      "batches": 2, "units_per_batch": 6}
            for station, x, y in [("empty", si + 1, si + 1), ("fill", fi + 4, si + 10),
                                  ("closure", si + 7, si + 4), ("finished", si + 10, si + 7)]:
                for suffix, z in [("entry", 1), ("work", si + 2)]:
                    recipe[station + "_" + suffix] = point(x, y, z, f"{flavor} {size} {station} {suffix}")
            recipes.append(recipe)
    devices = {name: {"port": "SIM_MOTION", "address": i + 1} for i, name in enumerate(MOTORS)}
    for i, name in enumerate([head for _, head, _ in FLAVORS] + ["Gripper", "CapFeeder", "ContainerSensor"]):
        devices[name] = {"port": "SIM_PROCESS", "address": i + 1}
    registry = {"configuration_note": "Synthetic simulation endpoints; not physical serial ports or commissioned addresses.",
                "devices": devices}
    variables = {"production_points": points, "recipes": recipes,
                 "last_x_pr": -1, "last_y_pr": -1, "last_z_pr": -1, "completed": 0}
    return recipes, registry, variables


def plan(r):
    """One container's operation plan. Values may be concrete or DSL placeholders."""
    return [
        ("trigger", "Infeed", r["infeed_pr"]), ("ready", "Infeed", 4000, "EMPTY_FEED_TIMEOUT"),
        ("move", r["empty_entry"]), ("move", r["empty_work"]), ("coil", "Gripper", 1),
        ("present", "ContainerSensor", 2000, "NO_CONTAINER"), ("move", r["empty_entry"]),
        ("move", r["fill_entry"]), ("move", r["fill_work"]),
        ("volume", r["fill_device"], r["volume_ml"]), ("coil", r["fill_device"], 1),
        ("ready", r["fill_device"], 20000, "FILL_TIMEOUT"),
        ("read_volume", r["fill_device"]), ("coil", r["fill_device"], 0), ("move", r["fill_entry"]),
        ("move", r["closure_entry"]), ("move", r["closure_work"]),
        ("coil", "CapFeeder", 1), ("trigger", "Closure", r["closure_pr"]),
        ("ready", "Closure", 5000, "CLOSURE_TIMEOUT"), ("coil", "CapFeeder", 0),
        ("move", r["closure_entry"]), ("move", r["finished_entry"]),
        ("move", r["finished_work"]), ("coil", "Gripper", 0),
        ("trigger", "Outfeed", r["outfeed_pr"]), ("ready", "Outfeed", 4000, "OUTFEED_TIMEOUT"),
        ("move", r["finished_entry"]), ("count",),
    ]


def raw_write(device, register, value, fc=6):
    data = 0xFF00 if fc == 5 and value else value
    pdu = f"{fc:02X} {register >> 8:02X} {register & 255:02X} {data >> 8:02X} {data & 255:02X}"
    return {"name": f"Write {device} register {register:04X}", "type": "autorequestresponse",
            "protocol": "modbus", "device": device, "request": pdu, "response": pdu}


def raw_read(device, register, key, count=1):
    return {"name": f"Read {device} {key}", "type": "readAndCache", "protocol": "modbus",
            "device": device, "request": f"03 {register >> 8:02X} {register & 255:02X} 00 {count:02X}",
            "cacheKey": key, "cacheStartByte": 2, "cacheLength": count * 2,
            "cacheEndian": "big", "valueType": "int" if count == 2 else "uint16"}


def raw_poll(device, timeout, code, register=0):
    return {"name": f"Wait for {device}", "type": "loopUntilResponse", "protocol": "modbus",
            "device": device, "request": f"04 00 {register:02X} 00 01", "expectedResponse": "04 02 00 01",
            "intervalMs": 20, "timeoutMs": timeout, "maxIterations": timeout // 20 + 2,
            "errorCode": code, "timeoutMsg": code.replace("_", " ").lower()}


def raw_move(point):
    actions = []
    for axis, motors in [("x", ["X"]), ("y", ["Y1", "Y2"]), ("z", ["Z"])]:
        key = "last_" + axis + "_pr"
        actions.append({"type": "ifVariableDiffers", "variable": key, "value": point[axis],
                        "actions": [raw_write(m, 0x6002, 16 + point[axis]) for m in motors] +
                                   [{"type": "setVariable", "key": key, "value": point[axis]}]})
    actions.extend(raw_poll(m, 4000, "MOTION_TIMEOUT") for m in MOTORS[:4])
    actions.extend(raw_read(m, 0x602C, m + "_position", 2) for m in MOTORS[:4])
    actions.extend({"type": "updateUI", "uiTarget": "center" + a, "uiValue": "${" + m + "_position}"}
                   for a, m in [("X", "X"), ("Y", "Y1"), ("Z", "Z")])
    return actions


def expand_raw(plan_items, points):
    out = []
    for operation in plan_items:
        kind, *args = operation
        if kind == "move":
            out.extend(raw_move(points[args[0]]))
        elif kind == "trigger": out.append(raw_write(args[0], 0x6002, 16 + args[1]))
        elif kind == "coil": out.append(raw_write(args[0], 0, args[1], 5))
        elif kind == "volume": out.append(raw_write(args[0], 0x0010, args[1]))
        elif kind in ("ready", "present"):
            out.append(raw_poll(*args, register=1 if kind == "present" else 0))
        elif kind == "read_volume":
            out.extend([raw_read(args[0], 0x0012, "dispensed_ml"),
                        {"type": "updateUI", "uiTarget": "dispensedMl", "uiValue": "${dispensed_ml}"}])
        elif kind == "count":
            out.extend([{"type": "incrementVariable", "key": "completed", "amount": 1},
                        {"type": "updateUI", "uiTarget": "completed", "uiValue": "${completed}"}])
        else: raise ValueError(kind)
    return out


class LayeredSuite(Migration):
    def call_group(self, filename, params=()):
        return {"type": "group", "template": "L3_group/" + filename + ".group.json", "params": list(params)}

    def define(self, name, body, args=(), **fields):
        value = {"type": "group", "name": name.replace("_", " "), "mode": "sequence"}
        if args:
            value["args"] = [{"index": i, "name": n, "type": t} for i, (n, t) in enumerate(args)]
        value.update(fields)
        value["body"] = body
        self.groups[name + ".group.json"] = value

    def setvar(self, key, value):
        act = self.action("set_state", "set_variable", [("key", "s"), ("value", "i32")], "b_saved")
        return self.node("Set state", act, [key, value])

    def ui(self, target, value):
        return self.leaf({"type": "updateUI", "name": "Update " + target,
                          "uiTarget": target, "uiValue": value}, "suite")

    def write(self, device, register, value, fc=6):
        # Frame identity is independent of device and argument value.
        original = raw_write(device, register, 0, fc)
        call = self.leaf(original, "suite")
        call["params"] = [value]
        return call

    def read(self, device, register, key, count=1):
        return self.leaf(raw_read(device, register, key, count), "suite")

    def ready(self, device, timeout, code, register=0):
        # Reuses the common parameterized L3 polling template built by Migration.
        return self.leaf(raw_poll(device, timeout, code, register), "suite")

    def build_templates(self):
        encode = self.action("encode_pr_trigger", "calculate", [("point", "u16")], "u16_trigger_word",
                             expression="16 + ${param_0}", arg_constraints={0: {"min": 0, "max": 15}})
        encode_call = self.node("Encode PR", encode, ["${motor_pr}"], result="u16_trigger_word")
        encode_call["result_key"] = "trigger_word"
        self.define("trigger_motor", [encode_call, self.write("${motor_device}", 0x6002, "${trigger_word}")],
                    [("motor_device", "s"), ("motor_pr", "u16")])
        read_state = self.action("read_state", "read_variable", [("key", "s")], "i32_previous_pr")
        get = self.node("Read state", read_state, ["${axis_state}"], result="i32_previous_pr")
        get["result_key"] = "previous_pr"
        self.define("move_axis_if_changed", [get,
            {"type": "group", "name": "Changed axis", "mode": "if",
             "condition": {"type": "expression", "expression": "${axis_pr} != ${previous_pr}"},
             "then": [
                 {"type": "group", "name": "Axis motors", "mode": "loop",
                  "loop": {"type": "foreach", "items": "axis_devices", "item_name": "active_motor"},
                  "body": [self.call_group("trigger_motor", ["${active_motor}", "${axis_pr}"])]},
                 self.setvar("${axis_state}", "${axis_pr}")], "else": []}],
            [("axis_state", "s"), ("axis_pr", "u16"), ("axis_devices", "arr")])
        refresh = [self.read(m, 0x602C, m + "_position", 2) for m in MOTORS[:4]]
        refresh += [self.ui("center" + a, "${" + m + "_position}") for a, m in [("X", "X"), ("Y", "Y1"), ("Z", "Z")]]
        self.define("refresh_position", refresh)
        move = [self.call_group("move_axis_if_changed", ["last_" + a + "_pr", "${production_points[point_index]." + a + "}", ds])
                for a, ds in [("x", ["X"]), ("y", ["Y1", "Y2"]), ("z", ["Z"])]]
        move += [self.ready(m, 4000, "MOTION_TIMEOUT") for m in MOTORS[:4]]
        move += [self.call_group("refresh_position")]
        self.define("move_production_point", move, [("point_index", "u16")])
        self.groups["move_production_point.group.json"]["args"][0].update({"min": 0, "max": 100})

    def render_plan(self, plan_items):
        body = []
        for kind, *args in plan_items:
            if kind == "move": body.append(self.call_group("move_production_point", args))
            elif kind == "trigger": body.append(self.call_group("trigger_motor", args))
            elif kind == "coil": body.append(self.write(args[0], 0, args[1] * 0xFF00, 5))
            elif kind == "volume": body.append(self.write(args[0], 0x0010, args[1]))
            elif kind in ("ready", "present"): body.append(self.ready(*args, register=1 if kind == "present" else 0))
            elif kind == "read_volume": body += [self.read(args[0], 0x0012, "dispensed_ml"), self.ui("dispensedMl", "${dispensed_ml}")]
            elif kind == "count":
                act = self.action("increment", "calculate", [("value", "i32")], "i32_count", expression="${param_0} + 1")
                call = self.node("Increment count", act, ["${completed}"], result="i32_completed")
                call["result_key"] = "completed"
                body += [call, self.ui("completed", "${completed}")]
            else: raise ValueError(kind)
        return body


def build(output):
    if output.exists() and any(output.iterdir()):
        raise ValueError("Output directory must be empty")
    recipes, registry, variables = specification()
    save(output / "device_registry.json", registry)
    save(output / "system_variables.json", variables)
    suite = LayeredSuite()
    suite.build_templates()
    symbolic = {key: "${recipes[recipe_index]." + key + "}" for key in recipes[0]}
    # Coil state values in plan are constants; recipe-dependent quantities stay typed placeholders.
    suite.define("produce_one_container", suite.render_plan(plan(symbolic)))
    suite.define("produce_batch", [suite.call_group("produce_one_container")], mode="loop", loop={"type": "count", "count": 6})
    initialization = [suite.setvar("last_" + a + "_pr", -1) for a in "xyz"] + [suite.setvar("completed", 0)]
    initialization += [suite.write(d, 0, 0, 5) for d in [head for _, head, _ in FLAVORS] + ["Gripper", "CapFeeder"]]
    initialization += [suite.call_group("move_production_point", ["${recipes[recipe_index].home_point}"])]
    shutdown = [suite.write(d, 0, 0, 5) for d in [head for _, head, _ in FLAVORS] + ["Gripper", "CapFeeder"]]
    suite.define("run_recipe", initialization + [{"type": "group", "name": "Two production batches", "mode": "loop",
                 "loop": {"type": "count", "count": 2}, "body": [suite.call_group("produce_batch")]}] + shutdown,
                 [("recipe_index", "u16")])
    for index, recipe in enumerate(recipes):
        raw_init = [{"type": "setVariable", "key": "last_" + a + "_pr", "value": -1} for a in "xyz"]
        raw_init += [{"type": "setVariable", "key": "completed", "value": 0}]
        raw_init += [raw_write(d, 0, 0, 5) for d in [head for _, head, _ in FLAVORS] + ["Gripper", "CapFeeder"]]
        raw_init += raw_move(variables["production_points"][recipe["home_point"]])
        raw = {"schema": "synthetic_monolithic_v1", "name": recipe["id"], "recipe": recipe,
               "actions": raw_init + [{"type": "repeat", "count": 2, "actions": [
                   {"type": "repeat", "count": 6, "actions": expand_raw(plan(recipe), variables["production_points"])}]}] +
                   [raw_write(d, 0, 0, 5) for d in [head for _, head, _ in FLAVORS] + ["Gripper", "CapFeeder"]]}
        save(output / "legacy" / (recipe["id"] + ".json"), raw)
        save(output / "layered/L4_flow" / (recipe["id"] + ".json"),
             {"type": "flow", "name": recipe["id"], "mode": "sequence", "body": [suite.call_group("run_recipe", [index])]})
    save(output / "layered/L1_action/all_actions.json", {"type": "action_bundle", "actions": list(suite.actions.values())})
    save(output / "layered/L2_node/all_nodes.json", {"type": "node_bundle", "nodes": list(suite.nodes.values())})
    for name, value in suite.groups.items(): save(output / "layered/L3_group" / name, value)
    return {"recipes": len(recipes), "production_points": len(variables["production_points"]),
            "devices": len(registry["devices"]), "L1": len(suite.actions), "L2": len(suite.nodes), "L3": len(suite.groups)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=ROOT / "examples/beverage_suite")
    print(json.dumps(build(parser.parse_args().output), indent=2))
