"""Create the authorized four-motor PR example from a legacy move baseline.

Device IDs and point allocations are example choices, not discovered hardware.
"""
import argparse
import copy
import json
from pathlib import Path

MANUAL = "https://www.leisaishop.com/uploadfiles/2311141001_67219.pdf"
MOTORS = {"X": ("X",), "Y": ("Y1", "Y2"), "Z": ("Z",)}


def save(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=4) + "\n", encoding="utf-8")


def write(device, address, value, name):
    frame = f"06 {address >> 8:02X} {address & 255:02X} {value >> 8:02X} {value & 255:02X}"
    return {"name": name, "type": "autorequestresponse", "protocol": "modbus", "device": device,
            "request": frame, "response": frame}


def walk(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from walk(child)
    elif isinstance(value, list):
        for child in value:
            yield from walk(child)


def revise(source):
    moves = [v for v in walk(source) if v.get("type") == "move"]
    if len(moves) != 24:
        raise ValueError("Expected the reviewed 24-move baseline; refusing a second migration")
    allocation = {}
    points = {}
    for axis, devices in MOTORS.items():
        definitions = [{"point": 0, "operation": "homing", "position": 0, "speed": 300}]
        targets = sorted({v["position"] for v in moves if v["axis"] == axis and not v.get("homing")})
        for number, position in enumerate(targets, 1):
            speeds = {v["speed"] for v in moves if v["axis"] == axis and v["position"] == position and not v.get("homing")}
            assert len(speeds) == 1
            definitions.append({"point": number, "operation": "absolute", "position": position, "speed": speeds.pop()})
            points[(axis, position)] = number
        assert len(definitions) <= 16
        for device in devices:
            allocation[device] = copy.deepcopy(definitions)
    refresh = []
    for device, key in [("X", "X_position"), ("Y1", "Y_position"), ("Y2", "Y2_position"), ("Z", "Z_position")]:
        refresh.append({"name": f"读取{device}轴当前位置并缓存", "type": "readAndCache", "protocol": "modbus",
                        "device": device, "request": "03 60 2C 00 02", "cacheKey": key,
                        "cacheStartByte": 2, "cacheLength": 4, "cacheEndian": "big", "valueType": "int"})
    for axis in ("X", "Y", "Z"):
        refresh.append({"name": f"更新主页{axis}坐标", "type": "updateUI", "uiTarget": "center" + axis,
                        "uiValue": "${" + axis + "_position}"})
    changes = []

    def transform(value):
        if isinstance(value, list):
            result = []
            for child in value:
                if isinstance(child, dict) and child.get("type") == "move":
                    axis = child["axis"]
                    point = 0 if child.get("homing") else points[(axis, child["position"])]
                    # Send Y1/Y2 triggers consecutively before the shared wait.
                    for device in MOTORS[axis]:
                        result.append(write(device, 0x6002, 0x10 + point, f"{device}: trigger PR{point} ({child['name']})"))
                    waits = [copy.deepcopy(x) for x in child["actions"] if x.get("type") == "timer"]
                    assert len(waits) == 1
                    result.extend(waits)
                    result.extend(copy.deepcopy(refresh))
                    changes.append({"name": child["name"], "axis": axis, "motors": list(MOTORS[axis]),
                                    "point": point, "position": child["position"], "wait_seconds": waits[0]["seconds"]})
                elif isinstance(child, dict) and child.get("device") == "AxisY" and "actions" not in child:
                    for device in ("Y1", "Y2"):
                        clone = copy.deepcopy(child)
                        clone["device"] = device
                        clone["name"] = device + ": " + clone["name"]
                        if device == "Y2":
                            for field in ("cacheKey", "errorCode"):
                                if field in clone:
                                    clone[field] += "_Y2"
                        result.append(transform(clone))
                else:
                    result.append(transform(child))
            return result
        if isinstance(value, dict):
            result = {k: transform(v) for k, v in value.items()}
            if result.get("device") in ("AxisX", "AxisZ"):
                result["device"] = {"AxisX": "X", "AxisZ": "Z"}[result["device"]]
            if result.get("type") == "move_group":
                result["type"] = "device_op"
            return result
        return value

    revised = transform(source)
    revised["version"] = "2.0"
    revised["lastModified"] = "2026-10-02"
    revised["description"] = "Beverage production: call preconfigured PR points for X/Y1/Y2/Z; refresh all position caches and three UI coordinates after each logical move. Device commissioning is completed separately."
    mapping = {"status": "example allocation authorized by user; not a discovered live-device map",
               "manual": MANUAL, "pr_base": "0x6200 + 8 * point", "pr_trigger": "write 0x0010 + point to 0x6002",
               "position_read": "03 60 2C 00 02, signed 32-bit big endian",
               "device_addresses": {"X": 1, "Y1": 2, "Y2": 3, "Z": 4},
               "y_pair": "Same target and positive direction; consecutive bus triggers, not hardware-synchronized motion.",
               "ui_y_source": "Y1 -> Y_position -> centerY; Y2 -> Y2_position (cached separately)",
               "setup_precondition": "PR points, homing mode and device readiness are configured during one-time commissioning; production only calls the stored points.",
               "completion": "Original fixed 2s/3s waits retained; not claimed to prove arrival.",
               "points": allocation, "moves": changes}
    assert not any(x.get("type") in ("move", "move_group") for x in walk(revised))
    return revised, mapping


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--mapping", type=Path, required=True)
    args = parser.parse_args()
    revised, mapping = revise(json.loads(args.source.read_text(encoding="utf-8-sig")))
    save(args.output, revised)
    save(args.mapping, mapping)
    print(json.dumps({"converted_moves": len(mapping["moves"]), "motor_moves": sum(len(m["motors"]) for m in mapping["moves"]),
                      "point_counts": {k: len(v) for k, v in mapping["points"].items()}}, indent=2))
