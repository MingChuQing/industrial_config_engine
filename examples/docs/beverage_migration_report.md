# Beverage Filling System: Monolithic Configuration → Four-Layer Configuration — Migration Comparison Report (public and reproducible)

> This report is the **public, reproducible** sample of the "monolithic vs. four-layer configuration comparison" in the paper "A Data-Driven Industrial Control Configuration Language".
> All numbers can be reproduced directly by counting the files in this repository.

## 1. Subjects of Comparison

| Approach | Location | Description |
|------|------|------|
| Old approach (monolithic configuration) | `examples/beverage_legacy/beverage_filling_legacy.json` | 12-step process, 13 devices, all configuration in a single JSON, mixing process orchestration with device-level Modbus frames |
| New approach (four-layer) | `examples/example4/` | L1 21 Actions, L2 13 Nodes, L3 17 parameterized groups, L4 one 12-step flow |

## 2. Size Comparison

| Metric | Old approach (monolithic) | New approach (four-layer) | Change |
|------|----------------|----------------|------|
| Number of files | 1 | 20 | Separated by layer |
| Total lines | 4082 | 1319 | **−67.7%** |
| Total bytes | 215,962 B | 35,241 B | **−83.7%** |
| Modbus frames (occurrences of `"request"`) | 239 | 0 in the orchestration layer (L3/L4); 18 in the device layer (L1/L2) | Device details frozen in L1/L2 |
| Group references / unique group definitions | Verbatim repetition | 29 references ← 17 parameterized groups | Reuse rate 41.4% |

Reuse distribution (group references in the L4 flow):

| Group | Reference count |
|----|----------|
| Filling cycle fill_cycle | 4 (four stations) |
| Four-station bottle detection pos_detect | 4 |
| Three-axis movement axis_move_seq | 4 (four filling stations) |
| Level recheck level_recheck | 2 |
| Valve control cycle rinse_cycle | 3 (rinse/disinfect/drain) |
| The remaining 12 groups | 1 each |

## 3. Key Conclusions

1. **Measurable layer isolation**: the orchestration layer (L3/L4) no longer contains any Modbus frames; all device details (register addresses,
   coil addresses, hexadecimal messages) are frozen in L1/L2, and the LLM only generates combinations of reference names.
2. **Reuse is parameterization**: the same "filling cycle" (open valve → metering → close valve) is defined once and reused four times with different parameters,
   replacing the verbatim repetition in the monolithic approach.
3. **Behavioral equivalence**: the two approaches describe the same filling process with an identical control flow; the migration only changes how the knowledge is organized.

## 4. Reproduction

```bash
# Count the old approach
python -c "import json;d=open('examples/beverage_legacy/beverage_filling_legacy.json',encoding='utf-8').read();print('lines',d.count(chr(10))+1,'frames',d.count('\"request\"'))"

# Count the new approach (four-layer)
find examples/example4 -name '*.json' | xargs wc -l
```

## 5. Coordinate Table (iDM-RS absolute coordinates, in pulses)

| Station | X | Y | Z |
|------|-------|-------|--------|
| Origin | 0 | 0 | 0 |
| Bottle pickup (feeding) | 50000 | 0 | 80000 |
| Filling station 1 | 120000 | 60000 | 150000 |
| Filling station 2 | 132000 | 60000 | 150000 |
| Filling station 3 | 144000 | 60000 | 150000 |
| Filling station 4 | 156000 | 60000 | 150000 |
| Capping | 170000 | 60000 | 150000 |
| Coding/marking | 220000 | 60000 | 150000 |
| Discharge | 260000 | 120000 | 80000 |

> Note: in the four-layer approach, the L2 layer is a schematic wrapping (each node corresponds to one L1 Action); the complete absolute-positioning command sequence
> (enable → PR0 mode → position high/low → speed → trigger; see `examples/device_examples/` and the iDM-RS manual)
> can be expanded into a multi-node combination inside L3 groups as needed. This comparison focuses on the four-layer separation of knowledge organization and size reduction, and does not imply
> byte-for-byte equivalence of execution semantics.
