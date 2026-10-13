# Browser execution demo

[Open the public demo](https://mingchuqing.github.io/industrial_config_engine/demo.html).

Click **Run demo** to execute both original Python interpreters in a browser Web Worker. No server-side execution, account, installation, or physical devices are required. The first run loads approximately 18 MB of runtime and data; subsequent runs reuse the worker.

- Nine recipes: orange, apple, grape × 2 L, 330 mL, 250 mL.
- Three selectable representative faults: low CIP pressure, failed level recheck, insufficient flow. These use the original diagnostic recipe (orange, 2 L).
- Full benchmark: all 15 normal recipe pairs and 22 diagnostic fault pairs across example4/example5. Each pair runs monolithic and layered configurations separately.
- Download the fresh result JSON or the complete timed trace for a selected recipe/fault.
- English and Chinese interface, with phone layout and cancellation.

## Evidence and boundaries

`bundle.json` contains unchanged Python sources and saved JSON configurations from commit `1acf6941054bcf8645512a6e818dbbf1e8f25578`. Sources are normalized to UTF-8/LF when bundled; `manifest.json` records their SHA-256 hashes and frozen source commit. `browser_demo.py` only selects cases, calls the original interpreters, compares the fresh result dictionaries against archived reports, and prepares downloads. Full-benchmark mode also recomputes the configuration-size comparison.

A mismatch raises an error; it is not replaced with a saved successful result. The page does not call an LLM, generate new configurations, execute the C++ mutation study, control physical devices, or demonstrate engineering-review reliability. Virtual elapsed process time and actual browser Python execution time are displayed separately.

## Files and runtime

`demo.mjs` handles the interface; `demo-worker.mjs` mounts bundled files in the browser Python filesystem and imports `browser_demo.py`. Original simulator sources remain unchanged in `scripts/`.

The self-hosted runtime is **Pyodide 314.0.7**, from the official [core release archive](https://github.com/pyodide/pyodide/releases/download/314.0.7/pyodide-core-314.0.7.tar.bz2), with its [license](pyodide/LICENSE). The runtime uses WebAssembly and must be served over HTTPS or localhost; opening `demo.html` directly via `file://` does not support its module worker. Serve `.mjs` as JavaScript and `.wasm` as `application/wasm` (GitHub Pages does this).

For complete local verification from the repository root:

```sh
python scripts/verify_beverage_full.py
```
