"""Negative controls for the independent configuration comparison.

Runs without modifying repository files or contacting any device.
"""
import argparse
import copy
import json
from pathlib import Path
import unittest
from unittest.mock import patch
import verify_beverage as audit

DIRECTORY = audit.ROOT / "examples/example4"


class BeverageMigrationTests(unittest.TestCase):
    def setUp(self):
        self.documents = {p.resolve(): audit.read(p) for p in DIRECTORY.rglob("*.json")}
        self.baseline = audit.verify(DIRECTORY)["contracts"]

    def reject(self, change):
        documents = copy.deepcopy(self.documents)
        change(documents)
        original_read = audit.read
        with patch.object(audit, "read", side_effect=lambda p: copy.deepcopy(documents[p.resolve()])
                          if p.resolve() in documents else original_read(p)):
            try:
                result = audit.verify(DIRECTORY)["contracts"]
            except (ValueError, KeyError):
                return
            self.assertNotEqual(result, self.baseline, "A changed behavior escaped verification")
            self.fail("verify() returned success for a changed contract")

    def nodes(self, docs):
        return docs[(DIRECTORY / "L2_node/all_nodes.json").resolve()]["nodes"]

    def actions(self, docs):
        return docs[(DIRECTORY / "L1_action/all_actions.json").resolve()]["actions"]

    def call(self, docs, predicate):
        def walk(value):
            if isinstance(value, dict):
                if value.get("type") == "node" and "template" in value and predicate(value):
                    yield value
                for child in value.values():
                    yield from walk(child)
            elif isinstance(value, list):
                for child in value:
                    yield from walk(child)
        return next(item for doc in docs.values() for item in walk(doc))

    def first_group(self, docs, mode):
        def find(x):
            if isinstance(x, dict):
                if x.get("type") == "group" and x.get("mode") == mode and (mode != "loop" or x.get("loop", {}).get("type") == "do-while"):
                    return x
                for v in x.values():
                    result = find(v)
                    if result is not None:
                        return result
            if isinstance(x, list):
                for v in x:
                    result = find(v)
                    if result is not None:
                        return result
        for value in docs.values():
            result = find(value)
            if result is not None:
                return result
        self.fail("No group found")

    def test_original_contracts_match(self):
        self.assertEqual(len(self.baseline), 354)
        self.assertEqual(sum("request" in x for x in self.baseline), 238)

    def test_all_move_refreshes(self):
        mapping = audit.read(audit.ROOT / "examples/beverage_legacy/motor_pr_map.json")
        self.assertEqual(set(mapping["points"]), {"X", "Y1", "Y2", "Z"})
        refreshes = [i for i, e in enumerate(self.baseline)
                     if e["kind"] == "cache" and e.get("cache_key") == "X_position"]
        self.assertEqual(len(refreshes), 24)
        for index, move in zip(refreshes, mapping["moves"]):
            self.assertEqual(self.baseline[index-1], {"step": self.baseline[index]["step"], "kind": "wait", "ms": move["wait_seconds"]*1000})
            for offset, motor in enumerate(("X", "Y1", "Y2", "Z")):
                cache = self.baseline[index+offset]
                self.assertEqual((cache["kind"], cache["device"], cache["request"]), ("cache", motor, "03 60 2C 00 02"))
            for offset, axis in enumerate("XYZ", 4):
                ui = self.baseline[index+offset]
                self.assertEqual((ui["kind"], ui["target"], ui["value"]), ("ui", "center"+axis, "${"+axis+"_position}"))
        report = audit.verify(DIRECTORY)
        self.assertEqual([e["device"] for e in report["omitted_redundant_triggers"]], ["Y1", "Y2"])
        self.assertEqual(report["reference_operations"], 356)

    def test_undefined_ui_cache(self):
        def change(d):
            item = self.call(d, lambda c: "${X_position}" in c.get("params", []))
            item["params"][1] = "${undefined_position}"
        self.reject(change)

    def test_missing_phase(self):
        self.reject(lambda d: d[(DIRECTORY / "L4_flow/main_flow.json").resolve()]["body"].pop())

    def test_wrong_device(self):
        self.reject(lambda d: self.call(d, lambda c: "set_motor_enable" in c["template"]).__setitem__("device", "WRONG_AXIS"))

    def test_wrong_address(self):
        self.reject(lambda d: self.actions(d)[0].__setitem__("request", "06 00 00 ${value_high} ${value_low}"))

    def test_wrong_command_value(self):
        self.reject(lambda d: self.call(d, lambda c: "set_motor_enable" in c["template"])["params"].__setitem__(0, 0))

    def test_group_motor_binding(self):
        def change(d):
            group = d[(DIRECTORY / "L3_group/trigger_motor_pr.group.json").resolve()]
            group["args"][0]["name"] = "unbound_motor"
        self.reject(change)

    def test_shared_refresh_missing_update(self):
        self.reject(lambda d: d[(DIRECTORY / "L3_group/refresh_all_motor_positions_and_ui.group.json").resolve()]["body"].pop())

    def test_motion_argument_binding(self):
        def change(d):
            group = d[(DIRECTORY / "L3_group/trigger_motor_pr.group.json").resolve()]
            group["body"][1]["params"][0] = "${undefined_trigger_word}"
        self.reject(change)

    def test_no_pr_programming_in_production(self):
        for event in self.baseline:
            if event["kind"] == "write" and event["device"] in ("X", "Y1", "Y2", "Z"):
                frame = bytes.fromhex(event["request"])
                address = int.from_bytes(frame[1:3], "big")
                self.assertFalse(0x6200 <= address < 0x6280)
        self.assertFalse(any(n.startswith("set_pr_") for n in audit.Layered(DIRECTORY).actions))

    def test_wrong_threshold(self):
        def change(d):
            node = next(n for n in self.nodes(d) if "judge" in n and n.get("on_failure"))
            node["judge"]["value"] += 1
        self.reject(change)

    def test_repeat_count(self):
        def change(d):
            stage = d[(DIRECTORY / "L3_group/step_06_filling.group.json").resolve()]
            group = next(g for g in stage["body"] if g.get("loop", {}).get("type") == "count")
            group["loop"]["count"] = 3
        self.reject(change)

    def test_misspelled_judge(self):
        def change(d):
            node = next(n for n in self.nodes(d) if "judge" in n)
            node["judge "] = node.pop("judge")
        self.reject(change)

    def test_poll_interval(self):
        self.reject(lambda d: self.first_group(d, "loop")["body"][1]["then"][0]["params"].__setitem__(0, 999))

    def test_poll_condition(self):
        self.reject(lambda d: self.first_group(d, "loop")["loop"].__setitem__("condition", "true"))

    def test_poll_timeout(self):
        def change(d):
            g = self.first_group(d, "loop")
            g["timeout_ms"] = g["loop"]["timeout_ms"] = 1
        self.reject(change)

    def test_decoder(self):
        self.reject(lambda d: next(a for a in self.actions(d) if "parse" in a)["parse"].__setitem__("endian", "little"))

    def test_ui_target(self):
        self.reject(lambda d: next(a for a in self.actions(d) if a["type"] == "ui_action")["data"].__setitem__("target", "WrongStatus"))

    def test_wait_type_overflow(self):
        def change(d):
            next(a for a in self.actions(d) if a["type"] == "wait")["args"][0]["type"] = "u16"
        self.reject(change)

    def test_point_out_of_range(self):
        self.reject(lambda d: d[(DIRECTORY / "system_variables.json").resolve()]["production_points"][0].__setitem__("x", 16))

    def test_point_axis_missing(self):
        self.reject(lambda d: d[(DIRECTORY / "system_variables.json").resolve()]["production_points"][0].pop("z"))

    def test_y_pair_order(self):
        def change(d):
            group = d[(DIRECTORY / "L3_group/move_production_point.group.json").resolve()]
            group["body"][1]["params"][2].reverse()
        self.reject(change)

    def test_skip_guard_disabled(self):
        def change(d):
            group = d[(DIRECTORY / "L3_group/move_axis_if_changed.group.json").resolve()]
            group["body"][1]["condition"]["expression"] = "${axis_pr} != -1"
        self.reject(change)

    def test_assumed_startup_position(self):
        self.reject(lambda d: d[(DIRECTORY / "system_variables.json").resolve()].__setitem__("last_x_pr", 0))

    def test_duplicate_endpoint(self):
        def change(d):
            devices = d[(DIRECTORY / "device_registry.json").resolve()]["devices"]
            devices["Y1"] = copy.deepcopy(devices["X"])
        self.reject(change)

    def test_request_placeholder(self):
        self.reject(lambda d: self.actions(d)[0].__setitem__("request", "06 ${missing} 00 00 01"))

    def test_station_byte_in_l1(self):
        def change(d):
            action = self.actions(d)[0]
            action["request"] = "01 " + action["request"]
        self.reject(change)

    def test_crc_bytes_in_l1(self):
        def change(d):
            action = self.actions(d)[0]
            action["request"] += " 00 00"
        self.reject(change)

    def test_nonmotor_l2_missing_device(self):
        def change(d):
            call = self.call(d, lambda c: c.get("device") == "PressureSensor")
            filename = call["template"].rsplit("/", 1)[1]
            next(n for n in self.nodes(d) if n["filename"] == filename).pop("device")
        self.reject(change)

    def test_nonmotor_connection_missing(self):
        self.reject(lambda d: d[(DIRECTORY / "device_registry.json").resolve()]["devices"].pop("PressureSensor"))


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--directory", type=Path, default=DIRECTORY)
    args, rest = parser.parse_known_args()
    DIRECTORY = args.directory.resolve()
    unittest.main(argv=[__file__, *rest])
