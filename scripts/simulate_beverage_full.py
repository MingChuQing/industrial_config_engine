"""Execute both saved representations of the full twelve-stage beverage line.

This deterministic hardware model is deliberately separate from the generator.
It preserves the existing independent legacy and layered interpreters, models
the restored valves and diagnostic sensors, and compares complete timed traces.
It is a synthetic software check, not physical machine commissioning evidence.
"""
import argparse
from collections import Counter
import hashlib
import json
from pathlib import Path
import struct

from simulate_beverage_suite import (
    LegacyInterpreter, LayeredInterpreter, SimulationError, VirtualLine, read, size,
)

ROOT = Path(__file__).resolve().parents[1]
FAULTS = {
    "infeed_jam": "EMPTY_FEED_TIMEOUT",
    "fill_offline": "COMM_OFFLINE",
    "closure_jam": "CLOSURE_TIMEOUT",
    "axis_alarm": "X_ALARM",
    "rinse_pressure_low": "RINSE_PRESSURE_LOW",
    "fill_pressure_low": "FILL_PRESSURE_LOW",
    "low_flow": "FLOW_NOT_REACHED",
    "level_fault": "RECHECK1_FAIL",
    "torque_low": "TORQUE_LOW",
    "coding_timeout": "CODE_POS_TIMEOUT",
    "cip_pressure_low": "CIP_PRESSURE_LOW",
}


class FullVirtualLine(VirtualLine):
    """Address-aware process I/O plus the seven existing motion mechanisms."""

    PROCESS_COILS = {("RinseValve", 0x12), ("CIPValve", 0x13), ("CIPValve", 0x14)} | {
        ("Relay", address) for address in range(0x08, 0x0F)
    }

    def __init__(self, registry, recipe, fault=None):
        super().__init__(registry, recipe, fault)
        if fault is not None and fault not in FAULTS:
            raise ValueError("Unknown full-line fault: " + fault)
        self.stage = None
        self.stages = []
        self.expected_stages = ["01"] + [f"{n:02}" for n in range(2, 11)] * (
            recipe["batches"] * recipe["units_per_batch"]
        ) + ["11", "12"]
        self.coil_addresses = {}
        self.active_since = {}
        self.rinsed = self.coded = self.labelled = False
        self.rinse_parts = {}
        self.level_checks = set()
        self.torque_checked = False
        self.unit_times = {}
        self.cip = {}
        self.unit_history = []
        self.alarm_checks = Counter()
        self.specification = {"family": 0, "can": 1, "small": 2}[recipe["format"]]

    def require(self, condition, code, message):
        if not condition: self.fail(code, message)

    def process_position(self, x):
        self.require(self.held and self.motor_pr["X"] == x and
                     self.motor_pr["Y1"] == self.motor_pr["Y2"] == self.specification + 1 and
                     self.motor_pr["Z"] == self.specification + 2,
                     "PROCESS_POSITION", "Wrong process station or no held container")

    def ui(self, target, value):
        if target == "stage":
            stage = str(value)
            self.require(len(self.stages) < len(self.expected_stages) and
                         stage == self.expected_stages[len(self.stages)],
                         "STAGE_ORDER", "Unexpected stage " + stage)
            if stage == "05":
                self.require(self.rinsed, "RINSE_INCOMPLETE", "Rinse, sanitize and drain must finish before filling")
            elif stage == "08":
                self.require(self.level_checks == set(range(0x38, 0x3C)),
                             "LEVEL_NOT_VERIFIED", "All four level sensors must pass")
            elif stage == "09":
                self.require(self.closed and self.torque_checked and self.unit_times.get("capping_ms", 0) >= 3000,
                             "CAPPING_INCOMPLETE", "Capping duration and torque check must finish")
            elif stage == "10":
                self.require(self.coded and self.labelled, "LABEL_INCOMPLETE", "Code and label must finish before discharge")
            elif stage == "11":
                self.require(len(self.delivered) == self.recipe["batches"] * self.recipe["units_per_batch"] and not self.held,
                             "CIP_SEQUENCE", "CIP requires completed production and no held container")
            elif stage == "12":
                self.require(self.cip.get("complete", False), "CIP_INCOMPLETE", "CIP must finish before standby")
            self.stage = stage
            self.stages.append(stage)
        super().ui(target, value)

    def write_coil(self, device, on):
        if device in self.FILL_X and on:
            self.require(self.rinsed, "UNRINSED_FILL", "A container must be rinsed before filling")
        if device == "Gripper" and not on and self.held:
            self.require(self.coded and self.labelled, "UNLABELLED_RELEASE", "A finished container must be coded and labelled")
        super().write_coil(device, on)
        if device == "Gripper" and on:
            self.rinsed = self.coded = self.labelled = self.torque_checked = False
            self.rinse_parts = {}
            self.unit_times = {}
            self.level_checks = set()

    def write_register(self, device, address, value):
        if device == "FlowMeter" and address == 1 and value == 0:
            self.dispensed_ml = 0
        if device == "Conveyor":
            self.require(address == 0x6002 and value in (0x10, 0x40), "CONVEYOR_COMMAND", "Unsupported conveyor command")
            self.registers[device][address] = value
            self.registers[device][0] = 1
            return
        if device == "Closure" and address == 0x6002:
            self.require(self.level_checks == set(range(0x38, 0x3C)),
                         "LEVEL_NOT_VERIFIED", "All four level checks must precede capping")
        if device == "Outfeed" and address == 0x6002:
            self.require(self.labelled and self.coded, "UNLABELLED_OUTPUT", "Code and label required for output")
        super().write_register(device, address, value)

    def record(self, kind, **fields):
        if kind == "finished_container":
            unit = {"rinsed": self.rinsed, "coded": self.coded, "labelled": self.labelled,
                    "level_checks": len(self.level_checks), "torque_checked": self.torque_checked,
                    **self.unit_times}
            self.unit_history.append(unit)
            self.delivered[-1].update(unit)
            fields.update(unit)
        super().record(kind, **fields)

    def process_coil(self, device, address, on):
        key = (device, address)
        old = self.coil_addresses.get(key, False)
        self.coil_addresses[key] = on
        self.coils[device] = any(state for (name, _), state in self.coil_addresses.items() if name == device)
        if on and not old:
            self.active_since[key] = self.now
            if self.stage == "04" and key in {("RinseValve", 0x12), ("CIPValve", 0x13), ("CIPValve", 0x14)}:
                self.process_position(13)
                if key == ("CIPValve", 0x13):
                    self.require(self.rinse_parts.get("water") and self.rinse_parts.get("pressure"),
                                 "RINSE_SEQUENCE", "Sanitizing requires completed rinse and pressure check")
                if key == ("CIPValve", 0x14):
                    self.require(self.rinse_parts.get("sanitize"), "RINSE_SEQUENCE", "Drain requires completed sanitizing")
            if key in {("Relay", 0x0C), ("Relay", 0x0D)}:
                self.process_position(14)
                self.require(self.closed and self.torque_checked, "LABEL_SEQUENCE", "Code and label require a capped container")
                if address == 0x0D: self.require(self.coded, "LABEL_SEQUENCE", "Coding must precede labelling")
            if self.stage == "11":
                self.require(not self.held, "CIP_SEQUENCE", "CIP cannot run while a container is held")
                if key == ("Relay", 0x0E):
                    self.require(self.coil_addresses.get(("CIPValve", 0x13)), "CIP_SEQUENCE", "Open CIP valve before pump")
                if key == ("CIPValve", 0x14):
                    self.require(self.cip.get("pressure_checked"), "CIP_SEQUENCE", "CIP circulation and pressure check must precede drain")
                if key == ("RinseValve", 0x12):
                    self.require(self.cip.get("drain_ms", 0) >= 30000 and not self.coil_addresses.get(("Relay", 0x0E)),
                                 "CIP_SEQUENCE", "Fresh water requires drained line and stopped pump")
        elif old and not on:
            elapsed = self.now - self.active_since.pop(key)
            if self.stage == "04":
                operation = {("RinseValve", 0x12): ("water", 5000), ("CIPValve", 0x13): ("sanitize", 3000),
                             ("CIPValve", 0x14): ("drain", 3000)}.get(key)
                if operation:
                    name, minimum = operation
                    self.require(elapsed >= minimum, "RINSE_DURATION", name + " duration is too short")
                    self.rinse_parts[name] = True
                    self.unit_times["rinse_" + name + "_ms"] = elapsed
                    self.rinsed = all(self.rinse_parts.get(p) for p in ("water", "pressure", "sanitize", "drain"))
            if key == ("Relay", 0x0B):
                self.require(elapsed >= 3000, "CAPPING_DURATION", "Capping actuator must run for three seconds")
                self.unit_times["capping_ms"] = elapsed
            elif key in {("Relay", 0x0C), ("Relay", 0x0D)}:
                self.require(elapsed >= 5, "LABEL_DURATION", "Code or label pulse did not complete")
                if address == 0x0C: self.coded = True; self.unit_times["coding_ms"] = elapsed
                else: self.labelled = True; self.unit_times["labelling_ms"] = elapsed
            if self.stage == "11":
                if key == ("CIPValve", 0x14):
                    self.require(elapsed >= 30000, "CIP_DURATION", "CIP drain must last thirty seconds")
                    self.cip["drain_ms"] = elapsed
                elif key == ("Relay", 0x0E):
                    self.cip["pump_ms"] = elapsed
                elif key == ("RinseValve", 0x12):
                    self.require(elapsed >= 20000, "CIP_DURATION", "Fresh water rinse must last twenty seconds")
                    self.cip["fresh_water_ms"] = elapsed
                elif key == ("CIPValve", 0x13):
                    self.require(self.cip.get("circulation_ms", 0) >= 120000 and self.cip.get("drain_ms", 0) >= 30000
                                 and self.cip.get("fresh_water_ms", 0) >= 20000,
                                 "CIP_DURATION", "CIP cleaning phases must all finish")
                    self.cip["valve_ms"] = elapsed
                    self.cip["complete"] = True

    def diagnostic(self, device, fc, address, count):
        """Return protocol bytes for known restored sensors, or None for base I/O."""
        if device in self.MOTORS and fc == 3 and address == 0x2203:
            self.alarm_checks[(self.stage, device)] += 1
            number = 1 if self.fault == "axis_alarm" and device == "X" else 0
            return bytes([fc, 2]) + number.to_bytes(2, "big")
        if device in self.MOTORS and fc == 3 and address == 0x1003:
            number = 0x60 if self.registers[device][0] else 0
            return bytes([fc, 2]) + number.to_bytes(2, "big")
        number = None
        floating = False
        if device == "PressureSensor" and fc == 3:
            floating = True
            if address == 0x10:
                if self.stage == "04":
                    number = 0.1 if self.fault == "rinse_pressure_low" else 0.25
                    self.rinse_parts["pressure"] = number >= 0.2
                elif self.stage == "11":
                    pump_start = self.active_since.get(("Relay", 0x0E), self.now)
                    elapsed = self.now - pump_start
                    self.require(elapsed >= 120000 and self.coil_addresses.get(("CIPValve", 0x13)),
                                 "CIP_DURATION", "CIP pressure is checked after two minutes circulation")
                    number = 0.1 if self.fault == "cip_pressure_low" else 0.4
                    self.cip["circulation_ms"] = elapsed
                    self.cip["pressure_checked"] = number >= 0.3
            elif address == 0x12:
                number = 0.05 if self.fault == "fill_pressure_low" else 0.2
        elif device == "FlowMeter" and fc == 3:
            if address == 0:
                floating = True
                number = float(self.dispensed_ml) * (0.8 if self.fault == "low_flow" else 1)
            elif address == 0x20: number = len(self.delivered)
        elif device == "Torque" and fc == 3 and address == 0x30:
            floating = True
            start = self.active_since.get(("Relay", 0x0B))
            capping_ms = self.now - start if start is not None else self.unit_times.get("capping_ms", 0)
            number = 2.5 if self.closed and capping_ms >= 3000 and self.fault != "torque_low" else 0.5
            self.torque_checked = number >= 2
        elif device == "BottleSensor" and fc == 4:
            if 0x34 <= address <= 0x37: number = int(self.held or (address == 0x34 and self.empty_ready))
            elif address == 0x40: number = int(bool(self.delivered) and not self.output_pending)
            elif address == 0x41: number = int(self.held)
            elif 0x42 <= address <= 0x45:
                number = int(self.held and self.rinsed and self.motor_pr["X"] == self.FILL_X[self.recipe["fill_device"]]
                             and self.motor_pr["Y1"] == self.motor_pr["Y2"] == self.specification + 10)
            elif address == 0x46: number = int(self.closed and self.torque_checked)
            elif address == 0x47: number = int(self.coded and self.labelled and self.fault != "coding_timeout")
        elif device == "LevelSensor" and fc == 4 and 0x38 <= address <= 0x3B:
            number = int(self.filled and not (self.fault == "level_fault" and self.stage == "07" and address == 0x38))
            if number and self.stage == "07": self.level_checks.add(address)
        if number is None: return None
        self.require(count == (2 if floating else (2 if device == "FlowMeter" else 1)),
                     "DIAGNOSTIC_READ_COUNT", "Wrong sensor read length")
        payload = struct.pack("<f", number) if floating else int(number).to_bytes(count * 2, "big")
        return bytes([fc, len(payload)]) + payload

    def transact(self, device, pdu):
        self.require(device in self.devices, "DEVICE", device)
        data = bytes.fromhex(pdu)
        self.require(len(data) == 5, "PDU", pdu)
        fc, address, count = data[0], int.from_bytes(data[1:3], "big"), int.from_bytes(data[3:5], "big")
        response = None
        if fc == 5 and (device, address) in self.PROCESS_COILS:
            self.require(count in (0, 0xFF00), "COIL_VALUE", pdu)
            self.process_coil(device, address, count == 0xFF00)
            response = data
        elif fc in (3, 4):
            response = self.diagnostic(device, fc, address, count)
        if response is None: return super().transact(device, pdu)
        spec = self.devices[device]
        self.record("modbus", device=device, port=spec["port"], address=spec["address"],
                    request=pdu, response=response.hex(" ").upper())
        self.advance(5)
        return response

    def validate_complete(self):
        self.require(self.stages == self.expected_stages, "STAGE_COVERAGE", "All twelve stages must execute in order")
        self.require(not self.pending and not self.held and not any(self.coils.values()) and
                     not any(self.coil_addresses.values()), "FINAL_STATE", "Actuators must be stopped at standby")
        self.require(all(self.motor_pr[d] == 0 for d in ("X", "Y1", "Y2", "Z")),
                     "HOME_POSITION", "All four positioning motors must finish at PR0")
        self.require(all(self.alarm_checks[(stage, motor)] for stage in ("01", "12") for motor in self.MOTORS[:4]),
                     "ALARM_CHECK_MISSING", "Initialization and standby must check every positioning motor")
        self.require(self.cip.get("complete", False), "CIP_INCOMPLETE", "CIP cycle missing")


def run_pair(root, recipe_index, fault=None, trace_dir=None):
    root = Path(root)
    variables = read(root / "system_variables.json")
    registry = read(root / "device_registry.json")
    recipe = variables["recipes"][recipe_index]
    documents = [("legacy", read(root / "legacy" / (recipe["id"] + ".json"))),
                 ("layered", read(root / "layered/L4_flow" / (recipe["id"] + ".json")))]
    runs = []
    for representation, document in documents:
        line = FullVirtualLine(registry, recipe, fault)
        interpreter = LegacyInterpreter(line) if representation == "legacy" else LayeredInterpreter(root / "layered", line, variables)
        status = "SUCCESS"
        try:
            interpreter.run(document)
            line.validate_complete()
        except SimulationError as error:
            status = error.code
        if trace_dir:
            destination = Path(trace_dir)
            destination.mkdir(parents=True, exist_ok=True)
            suffix = "_" + fault if fault else ""
            with (destination / f"{recipe['id']}{suffix}_{representation}.jsonl").open("w", encoding="utf-8") as stream:
                for event in line.trace: stream.write(json.dumps(event, ensure_ascii=False, separators=(",", ":")) + "\n")
        runs.append((line, interpreter, status))
    left, right = runs[0][0], runs[1][0]
    if left.trace != right.trace:
        mismatch = next((i for i, (a, b) in enumerate(zip(left.trace, right.trace)) if a != b), min(len(left.trace), len(right.trace)))
        raise AssertionError(f"{recipe['id']}: trace mismatch at {mismatch}: {left.trace[mismatch:mismatch+1]} != {right.trace[mismatch:mismatch+1]}")
    assert runs[0][2] == runs[1][2] and left.delivered == right.delivered
    for key in ("completed", "last_x_pr", "last_y_pr", "last_z_pr"):
        assert runs[0][1].vars.values.get(key) == runs[1][1].vars.values.get(key), key
    if fault is None:
        assert runs[0][2] == "SUCCESS", f"{recipe['id']}: {runs[0][2]}"
        assert len(left.delivered) == recipe["batches"] * recipe["units_per_batch"] == runs[1][1].vars.get("completed")
        assert sum(p["volume_ml"] for p in left.delivered) == len(left.delivered) * recipe["volume_ml"]
    serialized = json.dumps(left.trace, ensure_ascii=False, separators=(",", ":"))
    return {"recipe": recipe["id"], "fault": fault, "status": runs[0][2], "trace_equal": True,
            "events": len(left.trace), "modbus_transactions": sum(e["kind"] == "modbus" for e in left.trace),
            "simulated_ms": left.now, "finished_containers": len(left.delivered),
            "filled_total_ml": sum(p["volume_ml"] for p in left.delivered),
            "stage_counts": dict(sorted(Counter(left.stages).items())), "cip": left.cip,
            "process_durations_ms": left.unit_history,
            "trace_sha256": hashlib.sha256(serialized.encode()).hexdigest(),
            "group_calls": runs[1][1].group_calls,
            "used_actions": sorted(runs[1][1].used_actions), "used_nodes": sorted(runs[1][1].used_nodes),
            "used_groups": sorted(runs[1][1].used_groups)}


def compare(root, trace_dir=None):
    root = Path(root)
    variables = read(root / "system_variables.json")
    results = [run_pair(root, i, trace_dir=trace_dir) for i in range(len(variables["recipes"]))]
    faults = [run_pair(root, 0, fault, trace_dir=trace_dir) for fault in FAULTS]
    for result in faults:
        assert result["status"] == FAULTS[result["fault"]], result
    actions = read(root / "layered/L1_action/all_actions.json")["actions"]
    nodes = read(root / "layered/L2_node/all_nodes.json")["nodes"]
    assert {a["filename"] for a in actions} == set().union(*(set(r["used_actions"]) for r in results))
    assert {n["filename"] for n in nodes} == set().union(*(set(r["used_nodes"]) for r in results))
    assert {"L3_group/" + p.name for p in (root / "layered/L3_group").glob("*.json")} == set().union(*(set(r["used_groups"]) for r in results))
    sizes = {"legacy": size(sorted((root / "legacy").glob("*.json"))),
             **{layer: size(sorted((root / "layered" / directory).glob("*.json")))
                for layer, directory in (("L1", "L1_action"), ("L2", "L2_node"), ("L3", "L3_group"), ("L4", "L4_flow"))},
             "devices": size([root / "device_registry.json"]), "points_and_recipes": size([root / "system_variables.json"])}
    per_recipe = [{"recipe": r["id"], "legacy": size([root / "legacy" / (r["id"] + ".json")]),
                   "L4": size([root / "layered/L4_flow" / (r["id"] + ".json")])} for r in variables["recipes"]]
    return {"status": "PASS", "provenance": "Synthetic deterministic simulation; not measured industrial deployment data.",
            "scope": "Independently interpreted saved legacy and L1-L4 files; exact timed trace equality, twelve-stage process interlocks, diagnostic faults and actual actuator durations.",
            "sizes": sizes, "per_recipe_sizes": per_recipe, "recipes": results, "fault_cases": faults}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", type=Path, default=ROOT / "examples/beverage_full/example5")
    parser.add_argument("--report", type=Path)
    parser.add_argument("--traces", type=Path)
    args = parser.parse_args()
    report = compare(args.root, args.traces)
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"status": report["status"], "sizes": report["sizes"],
                      "recipes": [{k: r[k] for k in ("recipe", "status", "trace_equal", "finished_containers", "simulated_ms", "stage_counts")} for r in report["recipes"]],
                      "fault_cases": [{k: r[k] for k in ("fault", "status", "trace_equal")} for r in report["fault_cases"]]}, indent=2))
