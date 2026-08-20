# Experiment Log: LLM Generation and Three-Gate Semantic Verification (archived for Section 3 of the paper)

> This file archives all the raw experimental records referenced in Section 3 of the paper "A Data-Driven Industrial Control Configuration Language".
> All entries can be reproduced from this repository; reproduction commands are given in Section 6.

## 1. Experimental Setup

- **LLM**: DeepSeek V4-Flash
- **Input**: device manuals (L1/L2 capability lists, i.e., the `L1_action/` and `L2_node/` under `examples/example1-3` in this repository) + natural-language process requirements (archived in the `.txt` files under `examples/docs/sample1_vacuum/`, `sample2_feeding/`, and `sample3_leak_test/`)
- **Output**: L3/L4 process-orchestration configurations (the `L3_group/` and `L4_flow/` under `examples/example1-3` in this repository)
- **Verification**: structural validation (at load time) → simulation execution (`demo_simulator`, before deployment) → semantic mapping (human review)

## 2. Test Cases and Configuration Size (counting basis: files archived in the repository)

| Case | Process | Devices | L1 Actions | L2 Nodes | L3 Groups | L4 Flows |
|------|------|------|--------|--------|------|--------|
| T1 | Vacuum control | 3 | 11 | 10 | 7 | 1 |
| T2 | Motor feeding | 4 | 18 | 22 | 12 | 1 |
| T3 | Leak test | 3 | 26 | 30 | 10 (7 built + 3 reused from T2) | 1 |

Configuration size (counted by file):

| Case | L3/L4 (LLM-generated) | L1/L2 (frozen layer, not visible to the LLM) | Controlled vocabulary (number of distinct reference names in L3/L4) |
|------|-------------------|------------------------------|------------------------------------------|
| T1 | 8 files / 424 lines / 11.1 KB | 8 files / 445 lines / 12.3 KB | 16 |
| T2 | 13 files / 537 lines / 17.0 KB | 14 files / 1085 lines / 32.8 KB | 30 |
| T3 | 11 files / 818 lines / 27.6 KB | 19 files / 2175 lines / 72.1 KB | 45 |

## 3. LLM Generation Records (session records)

| Case | Initial generation time (session records) | Correction rounds | Final verification |
|------|--------------------------|----------|----------|
| T1 | ~20 seconds | 1–2 rounds | Passed |
| T2 | ~45 seconds | 1–2 rounds | Passed |
| T3 | ~40 seconds | 1–2 rounds | Passed |

Correction method: manual correction, or the LLM regenerates the affected parts based on the error report from a verification gate.
The LLM's original drafts are archived in the text files under `examples/docs/sample1-3/`.

## 4. Defect Interception Records (during integration and acceptance of the three processes, 2026-08-17)

| # | Defect (example) | Found in | Interception gate |
|---|--------------|--------|----------|
| 1 | Illegal JSON: hexadecimal literals `0x0200`/`0x0201`/`0x01` | T1, T3 | Structural validation (parsing) |
| 2 | Missing node parameters (pump start/stop nodes did not provide register parameters) | T2 | Structural validation (parameter signature) |
| 3 | Filename typo `pump_down..json` | T1 | Structural validation (reference resolution) |
| 4 | Missing action layer / cross-directory references (T1 missing data layer, T3 cross-directory references to positioning/log) | T1, T3 | Structural validation (reference resolution) |
| 5 | Loop predicted variables that are only produced inside the loop body (`motion_done`, `sensor_triggered`, `pressure_rising`) | T2, T3 | Simulation execution (CONDITION_ERROR) |
| 6 | Broken variable chains (`leak_rate`, `leak_ok`, `p_start`, `final_pressure` never produced) | T2, T3 | Simulation execution (CONDITION_ERROR) |
| 7 | Loop type mismatch with intent (`while` should be `do-while`) | T2, T3 | Semantic mapping (human review of intent) |

## 5. Fault Injection (verifying the interception capability of the three gates, 2026-08-17)

| Injected defect | Configuration fragment | Interception result |
|----------|----------|----------|
| Condition references an undefined variable (typo `${pressue}`) | Loop condition do-while `${pressue} > 5` | CONDITION_ERROR: loop condition evaluation failed, 0 device writes |
| Always-true loop condition (`true`) | Loop condition while `true`, iteration cap 100 | STEP_BUDGET_EXCEEDED: loop iterations exceeded the cap (100), suspected infinite loop, 0 device writes |
| References a nonexistent node | body template `read_pressureX.r_u16_pressure` | REFERENCE_ERROR: node definition missing, 0 device writes |

## 6. Final Verification Run (measured output of demo_simulator, 2026-08-17)

Reproduction commands (VS 2022 Developer Command Prompt, repository root):

```
cmake --build build\simcheck
build\simcheck\bin\demo_simulator.exe
```

Measured results (loading the three configuration roots example1/2/3, 55 L1 Actions and 62 L2 Nodes in total):

| Case | Status | Simulation steps | Virtual time | Wall-clock time | Audit records | Device writes | Key result |
|------|------|----------|----------|----------|----------|----------|----------|
| T1 Vacuum control | SUCCESS | 923 | 88.48 s | ≈0.2 s | 192 records | 2 writes | Pressure 65000→3 Pa, pump stopped, leak rate 0.0% |
| T2 Motor feeding | SUCCESS | 104 | 0.50 s | ≈0.03 s | 46 records | 12 writes | position_1, position_2 saved |
| T3 Leak test | SUCCESS | 193 | 0.84 s | ≈0.07 s | 168 records | 14 writes | test_result=PASS, 79 OPERATOR_CONFIRM records |
| Injection 1 (typo) | CONDITION_ERROR | — | — | — | 1 record | 0 writes | Intercepted before load/execution |
| Injection 2 (always-true loop) | STEP_BUDGET_EXCEEDED | — | — | — | 101 records | 0 writes | Intercepted by iteration cap |
| Injection 3 (missing node) | REFERENCE_ERROR | — | — | — | 0 records | 0 writes | Intercepted before execution |

Note: T1's step count and timing fluctuate slightly with the virtual pressure-decay model (923 steps is one representative run); audit and write counts are tallied for that run.

## 7. Structural Validation and Human Review Records

- **Structural validation**: performed at load time (JSON parsing + per-layer `validate()` + full reference resolution); all three processes took <1 second.
- **Semantic mapping (human review)**: the L3/L4 configuration is reverse-translated into natural language and flowcharts and compared against the original requirements; time taken was about 2 minutes for T1, 6 minutes for T2, and 6 minutes for T3 (including correction confirmation). The main semantic issue found during review is item 7 of Section 4 (loop type mismatch with intent).
