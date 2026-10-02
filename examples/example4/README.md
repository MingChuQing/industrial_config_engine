# Beverage production configuration

Start at `L4_flow/main_flow.json`. Preload the following two files before
running a flow or movement group:

- `device_registry.json`: logical device names map to `port` and Modbus
  station `address`. The COM3/COM4 assignments are examples to replace with
  installed settings. Devices may share a port if their station addresses
  differ. Duplicate port/address pairs and addresses outside 1..247 are rejected.
- `system_variables.json`: `production_points` is a predefined array of full
  XYZ targets. Each row contains `x`, `y`, `z` (PR numbers 0..15) and `wait_ms`.
  `last_x_pr`, `last_y_pr`, `last_z_pr` start at -1 (unknown).

A production flow calls `move_production_point.group.json(point_index)` with
only one integer. Indices 0..100 are allowed, and the selected row must exist.
The current example contains 17 distinct full XYZ rows, used by 21 production
move calls. For example, a row `{ "x": 3, "y": 2, "z": 5, "wait_ms": 2000 }`
selects X PR3, Y1/Y2 PR2 and Z PR5.

The reusable groups have these responsibilities:

1. `trigger_motor_pr.group.json(device, pr)` sends one PR trigger.
2. `move_axis_if_changed.group.json(state_key, pr, devices)` compares the
   requested PR number with the last successfully issued number. An unchanged
   axis emits no trigger; Y changes trigger Y1 then Y2. The cache is invalidated
   before issuing commands and updated only when every motor returns success.
3. `move_production_point.group.json(point_index)` reads the XYZ row, invokes
   that same axis group for X, Y and Z, waits once, and refreshes four position
   caches and the three coordinate displays. Even a repeated full point retains
   its existing wait and refresh.
4. `home_axis_and_refresh.group.json(state_key, devices)` is used for the three
   initial homing operations, before a complete XYZ state has been established.

The cache records acknowledged commands, not measured arrival. Fixed waits
retain their original role. After a restart, manual motion, device reconnect,
or PR-table reconfiguration, invalidate affected `last_*_pr` values to -1
before reuse. Point definitions and homing mode are established during device
commissioning; no PR-table programming is added to production.

Of the original 28 motor triggers, two redundant Y1/Y2 PR1 commands at stage 5
are now intentionally skipped. There are 26 motor triggers; all 24 wait/refresh
sequences remain. Raw Modbus traces therefore differ from the original by
these two commands. The verifier reports the omitted commands explicitly.

## Reusable polling and readable names

All 21 polling sites call `L3_group/poll_sensor_until_ready.group.json`.
The ordered parameters are: L2 check-node reference, device alias, interval
milliseconds, timeout milliseconds, maximum iterations, error code and error
message. The selected L2 node must already exist in the loaded node registry.
The group reads first, waits only after an unsuccessful check, and preserves
the original deadline, iteration budget and timeout report. Successful checks
do not incur an extra interval wait.

For example, the level check passes
`["L2_node/all_nodes.json/read_fc04_reg0038.r_b_poll_ready", "LevelSensor", 50, 8000, 162, "LEVEL_TIMEOUT", "Level not reached"]`.

Generated hashes were removed from all L1/L2 names and L3 filenames/references.
Read names identify the function and register (`read_fc04_reg0038`), with
`_byte` or `_le` where decoding differs. Distinct diagnostic wrappers use their
error-code names (e.g. `_x_alarm`). The generator rejects semantic-name
collisions instead of overwriting a definition. Argument/return signatures
remain unchanged.

## Device binding

This applies to all 14 Modbus devices: X, Y1, Y2, Z, Conveyor, BottleSensor,
LevelSensor, FlowMeter, PressureSensor, Torque, Relay, FillingValve, RinseValve,
and CIPValve. Every Modbus L2 node declares `device`; wait, calculation and UI
nodes have no physical Modbus device to bind.


Modbus L2 definitions have `"device": "${device}"`. A node call may provide
`"device": "${motor_device}"` or a literal alias, overriding the L2 default.
The connection registry resolves that alias to port/address at execution time.
L1 holds register addresses and frame layouts, without repeating device aliases
in its argument signature. Legacy L1 `slave_id` binding remains supported when
there is no explicit device field.

L1 `request` and write-echo `response` contain only the Modbus PDU: function
code followed by register/coil address and data. For example, `03 60 2C 00 02`
has no device station byte and no CRC bytes. Register addresses remain in L1.
An RTU transport adapter must prepend the selected device's station address
and append CRC when sending, and check/remove station and CRC before passing
the response PDU to L1 decoding. Thus `parse.start_byte` is relative to the
response PDU; the current value 2 skips function code and byte count.

Device selection and RTU framing are shared for motors, valves and sensors.
The same L1 action can be reused on devices with compatible register layouts
and operation semantics; different register maps still need corresponding L1
payload definitions.

The registry `address` is the Modbus station address, not an L1 register address.
The simulator records resolved routes and uses virtual devices; it does not
open serial ports. A hardware adapter must use this connection metadata to
address the real device. Serial baud/parity settings belong to that adapter's
port configuration.

## Loading and verification

This configuration requires the engine changes delivered with it. The host
application loads the two JSON documents once before production:

```cpp
Executor executor;
executor.addRoot("examples/example4");
if (!executor.loadLayers()) throw std::runtime_error("Cannot load layers");
ExecContext ctx;
nlohmann::json registry, variables;
std::ifstream device_file("examples/example4/device_registry.json");
std::ifstream variable_file("examples/example4/system_variables.json");
device_file >> registry;
variable_file >> variables;
std::string error;
if (!ctx.devices.configure(registry, error)) throw std::runtime_error(error);
for (auto it = variables.begin(); it != variables.end(); ++it)
    ctx.vars.set(it.key(), it.value());
// The L4 flow does not overwrite these externally preloaded variables.
auto result = executor.runGroupFile(
    "examples/example4/L3_group/move_production_point.group.json",
    nlohmann::json::array({0}), ctx);
```

Variable paths support object members and array indices, for example
`${production_points[point_index].motors}` and `${selected_motor.device}`.
An index is a nonnegative integer literal or an integer variable. Exact
placeholder values preserve their JSON type; other text uses interpolation.

```console
python scripts/verify_beverage.py
python scripts/test_beverage_migration.py -v
cmake -S . -B build -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The C++ test checks routing for all 14 device aliases, L2 binding, PR bounds, and all 17 movement
rows, same-point skipping, single-axis changes and Y-pair failure/retry. Static comparison covers the complete production flow. Polling tests also cover immediate readiness, delayed readiness,
timeout diagnostics and invalid node references. Neither test establishes
physical-device equivalence. See `../docs/beverage_migration_report.md`
and `../docs/beverage_equivalence_report.json` for scope and statistics.
