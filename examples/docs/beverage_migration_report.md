# Beverage production configuration

The immutable reference is `examples/beverage_legacy/beverage_filling_legacy.json`.
The current configuration is `examples/example4/`. Both exclude PR-table
commissioning. The original reference has not been changed to disguise the
new skip behavior.

## Production-point semantics

A production point is a full XYZ tuple, with each component selecting a stored
PR point 0..15. The caller supplies only a production point index (0..100;
current table: 17 rows). L3 reads the tuple and uses one reusable axis group
for X, paired Y1/Y2, and Z. Only changed axes are triggered, in X/Y/Z order.
Initial per-axis homing is retained separately and establishes the cache.
There are 21 full-point calls after the initial three homing calls.

The cache starts at -1 and records the last successfully issued PR number.
The axis cache is invalidated before changed-axis commands, then updated only
if all associated motors acknowledge success. A partial Y failure leaves Y
unknown, so both motors are retried on the next call. The cache does not prove
physical arrival. Invalidate it after manual motion/reconnection/reconfiguration.
Original 2s/3s waits and four-position/three-coordinate refreshes remain at all
24 original move locations, including repeated full-point calls.

All 14 devices have entries in `device_registry.json`; all 58 Modbus L2 nodes
have explicit device binding. All 48 Modbus L1 definitions contain PDUs only,
without station or CRC bytes. The simulated registry resolves and audits the
port/address; physical RTU framing belongs to a hardware transport adapter.

## Polling reuse and names

All 21 polling sites use one shared L3 definition, with parameters for the
loaded L2 check node, device, interval, timeout, iteration budget and timeout
diagnosis. This revision preserves every one of the preceding revision's 354
ordered observable contracts; it adds no new behavioral differences.
All generated hash identifiers were replaced with register/decoder/error-code
names, and every reference was updated. The generator detects naming collisions.

## Verification and deliberate difference from the reference

```console
python scripts/verify_beverage.py --report examples/docs/beverage_equivalence_report.json
python scripts/test_beverage_migration.py -v
cmake -S . -B build -DBUILD_DEMOS=OFF -DBUILD_TESTING=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

The requested skip rule removes two redundant commands at stage 5: Y1 and Y2
were already at PR1, so a second `06 60 02 00 11` trigger is omitted. Reference
operation indices 126 and 127 (zero-based) are recorded in the JSON report.
The independent verifier applies this explicit rule to the original ordered
contracts and compares every retained operation with the expanded four layers.
This is equality after a declared transformation, not raw trace equality.

| Operation | Original | Changed-axis configuration |
|---|---:|---:|
| Write request and echo | 88 | 86 |
| Read and compare | 19 | 19 |
| Read and cache | 112 | 112 |
| Polling contracts | 21 | 21 |
| Fixed waits | 32 | 32 |
| UI updates | 84 | 84 |
| Total | 356 | 354 |
| Modbus operation sites | 240 | 238 |

All L1/L2 definitions and L3 groups are reachable. Pure calculations and cache
reads/writes are internal and excluded from the observable contract counts.
Thirty-one Python tests cover behavior changes and negative controls. C++
tests exercise all 14 device routes, all 17 point rows, repeated points,
Z-only changes, paired Y failure/retry, unknown startup state and index/PR bounds.
Shared-poll tests cover immediate and delayed readiness, timeout diagnostics,
and invalid node references. FC04 now reads the requested virtual register.
The existing T1/T2/T3 demos are checked separately for regressions.

Static expansion and focused simulator tests do not establish hardware
equivalence or full-line simulator equivalence. Separate FC03/FC04 register spaces, byte/float decoding,
UI effects and failure propagation are not fully modeled by the simulator.
The successful-command cache therefore represents simulator acknowledgements,
not physical motion feedback. L2 retains its transport-policy assumptions.

## Representation size

Both formats use four-space indentation and one trailing newline. Scripts,
documentation and verification reports are excluded. The original has 4,529
lines and 230,470 UTF-8 bytes. Current components:

| Component | Files | Lines | UTF-8 bytes |
|---|---:|---:|---:|
| L1 | 1 | 735 | 22,208 |
| L2 | 1 | 945 | 30,652 |
| L3 | 19 | 1,806 | 50,074 |
| L4 | 1 | 79 | 2,500 |
| Point table and initial cache | 1 | 126 | 2,758 |
| Device registry | 1 | 61 | 1,375 |
| Total | 24 | 3,752 | 109,567 |

Four-layer files alone: 3,565 lines. With point/cache variables: 3,691
lines. Including the device registry: 3,752 lines, 777 (17.16%) fewer than
the original 4,529 and 629 fewer than the preceding 4,381-line revision.
The original omits the connection registry. Counts include every current
configuration JSON with the same indentation. Internal state tracking and
serialization affect size; this does not isolate the effect of layering.

## Regeneration

```console
python scripts/rebuild_beverage.py --output build/beverage-candidate
python scripts/verify_beverage.py --directory build/beverage-candidate
```

Use an empty candidate directory and verify before replacing example4. The
generator derives full XYZ targets by tracking the original axis targets after
initial homing. The independent verifier derives redundant-write omissions
separately. The original source SHA-256 is recorded in the report.
