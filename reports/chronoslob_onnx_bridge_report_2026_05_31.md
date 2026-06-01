# ChronosLOB ONNX Bridge Report - 2026-05-31

These are representative local measurements on this machine/environment, not portable performance claims.

This report covers research-model-to-systems integration using a tiny
ChronosLOB-style ONNX artefact. It does not claim predictive quality, trading
profitability, live trading capability, production model-serving infrastructure,
production-HFT infrastructure, SOTA modelling or a portable latency guarantee.

> **Update (2026-06-01):** this report describes the original hand-written
> deterministic **fixture**. The integration has since been upgraded to a
> **real, tiny trained ChronosLOB `DeepLOBModel`** exported from ChronosLOB;
> see [chronoslob_real_model_bridge_report_2026_06_01.md](chronoslob_real_model_bridge_report_2026_06_01.md).
> Both artefacts remain checked in and benchmarked side by side.

## What Was Integrated

- Added `data/models/chronoslob_tiny_fixture.onnx`.
- Added `data/models/chronoslob_tiny_fixture.metadata.json`.
- Added `tools/export_chronoslob_tiny_onnx.py` to regenerate/verify the fixture.
- Added C++ metadata loading/validation for the fixture contract.
- Added feature count/version compatibility checks before ONNX backend selection.
- Extended model diagnostics with model name, input shape and output shape.
- Updated optional ONNX benchmarks to use the ChronosLOB-style fixture when ONNX
  Runtime is available.
- Preserved dependency-light default builds and `LinearModel` fallback behavior.

The model is not trained. It is a deterministic fixture with a fixed linear
`Gemm` node over Asterion's four L2 features.

## ChronosLOB Status

- Local source path: `../ChronosLOB/chronos-lob`
- Commit: `2330a15cae6a8ba1ba1ad2df86d0983708c833bc`
- Dirty before this task: yes, unrelated local changes were already present
- Modified by this task: no

At the time of this fixture pass, the local ChronosLOB checkout had Torch model
definitions and conservative feature/safety docs, but did not yet have the
committed ChronosLOB export helper. The Asterion-side fixture documented the
simplified contract that was later complemented by the real export path.

## Model Metadata

| field | value |
|---|---|
| model name | `chronoslob_tiny_fixture` |
| export command | `python tools/export_chronoslob_tiny_onnx.py --output data/models/chronoslob_tiny_fixture.onnx --metadata-output data/models/chronoslob_tiny_fixture.metadata.json` |
| input | `features`, shape `1x4` |
| output | `score`, shape `1x1` |
| feature count | `4` |
| feature version | `1` |
| trained model | `false` |
| deterministic fixture | `true` |
| expected input | `[2.0, 1000.0, 0.5, 400.0]` |
| expected output | `[-0.08499999999999998]` |

Feature order:

1. `spread_ticks`
2. `mid_price_ticks`
3. `top_level_imbalance`
4. `top_level_quantity`

## Backend And Fallback

Default builds do not require ONNX Runtime. `LinearModel` remains the default
backend and deterministic fallback. The C++ ONNX Runtime dependency was not
installed by default in this local environment, so the dependency-light benchmark
run did not include ONNX rows.

The opt-in configure path was checked with `-DASTERION_USE_ONNXRUNTIME=ON`; CMake
reported ONNX Runtime was not found and the build still passed with fallback
enabled.

An additional local real-runtime attempt downloaded the official Windows ONNX
Runtime 1.20.1 archive and compiled Asterion with `ASTERION_HAVE_ONNXRUNTIME`.
That compile completed after rerunning Ninja with `-j1`, but the test executable
could not be loaded by this Windows environment (`0xc000007b`). `objdump` showed
the downloaded `onnxruntime.dll` depends on `api-ms-win-core-path-l1-1-0.dll`,
which is not present on this machine. No ONNX benchmark numbers were fabricated.

Feature compatibility checks are explicit:

- metadata loads and validates;
- expected input/output are deterministic;
- feature count mismatch reports `feature count mismatch`;
- feature version mismatch reports `feature version mismatch`;
- ONNX selection with incompatible metadata falls back to `LinearModel`.

## Allocation Split

- Fixture export/model files are generated outside runtime hot paths.
- Metadata/model load is separate from steady-state inference measurements.
- Caller-owned feature extraction + `LinearModel` scoring remains allocation-free
  after warm-up in the scoped test and benchmark rows.
- Vector-returning feature extraction still allocates one vector per call and is
  preserved only as a convenience API.
- ONNX model load/scoring allocations are reported only in builds with real ONNX
  Runtime. No ONNX allocation-free claim is made.

## Local Health Gate

Environment:

- OS: Windows
- Compiler: GCC 16.1.0 from MSYS2 UCRT64
- CMake: 4.3.3
- Ninja: 1.13.2
- CTest: 4.3.3
- Python used for bindings/tests: 3.11.9
- ONNX Python package: available for fixture generation
- C++ ONNX Runtime: not installed by default; official Windows 1.20.1 archive
  compiled, but runtime execution was blocked by the local Windows loader

Commands run:

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
& 'C:\Program Files\WindowsApps\PythonSoftwareFoundation.Python.3.11_3.11.2544.0_x64__qbz5n2kfra8p0\python3.11.exe' tools\export_chronoslob_tiny_onnx.py --output data\models\chronoslob_tiny_fixture.onnx --metadata-output data\models\chronoslob_tiny_fixture.metadata.json
& 'C:\Program Files\WindowsApps\PythonSoftwareFoundation.Python.3.11_3.11.2544.0_x64__qbz5n2kfra8p0\python3.11.exe' tools\export_chronoslob_tiny_onnx.py --verify
cmake -S . -B build-chronoslob-bridge -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_ENABLE_WARNINGS=ON -DASTERION_BUILD_PYTHON=ON
cmake --build build-chronoslob-bridge
ctest --test-dir build-chronoslob-bridge --output-on-failure
$env:PYTHONPATH=(Resolve-Path 'build-chronoslob-bridge\python').Path
& 'C:\Program Files\WindowsApps\PythonSoftwareFoundation.Python.3.11_3.11.2544.0_x64__qbz5n2kfra8p0\python3.11.exe' -m pytest python\tests
cmake --build build-chronoslob-bridge --target asterion_benchmarks
.\build-chronoslob-bridge\asterion_benchmarks.exe --json build-chronoslob-bridge\chronoslob_bridge_benchmark.json --no-text
.\scripts\run_demo.ps1 -BuildDir build-chronoslob-bridge -SkipBuild -PythonExe 'C:\Program Files\WindowsApps\PythonSoftwareFoundation.Python.3.11_3.11.2544.0_x64__qbz5n2kfra8p0\python3.11.exe'
cmake -S . -B build-chronoslob-bridge-onnx-fallback -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_ENABLE_WARNINGS=ON -DASTERION_BUILD_PYTHON=ON -DASTERION_USE_ONNXRUNTIME=ON
cmake --build build-chronoslob-bridge-onnx-fallback --target asterion_tests asterion_python_package
ctest --test-dir build-chronoslob-bridge-onnx-fallback --output-on-failure
$env:PYTHONPATH=(Resolve-Path 'build-chronoslob-bridge-onnx-fallback\python').Path
& 'C:\Program Files\WindowsApps\PythonSoftwareFoundation.Python.3.11_3.11.2544.0_x64__qbz5n2kfra8p0\python3.11.exe' -m pytest python\tests
Invoke-WebRequest -Uri https://github.com/microsoft/onnxruntime/releases/download/v1.20.1/onnxruntime-win-x64-1.20.1.zip -OutFile build-chronoslob-onnxruntime\onnxruntime.zip
Expand-Archive -LiteralPath build-chronoslob-onnxruntime\onnxruntime.zip -DestinationPath build-chronoslob-onnxruntime -Force
$src='build-chronoslob-onnxruntime\onnxruntime-win-x64-1.20.1'
$dst='build-chronoslob-onnxruntime\onnxruntime'
if (Test-Path $dst) { Remove-Item -LiteralPath $dst -Recurse -Force }
Move-Item -LiteralPath $src -Destination $dst
cmake -S . -B build-chronoslob-bridge-onnxrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_ENABLE_WARNINGS=ON -DASTERION_BUILD_PYTHON=ON -DASTERION_USE_ONNXRUNTIME=ON -DONNXRUNTIME_ROOT='C:\Users\Lenovo\Programming\Asterion\build-chronoslob-onnxruntime\onnxruntime'
cmake --build build-chronoslob-bridge-onnxrt --target asterion_tests asterion_benchmarks asterion_python_package -- -j1
ctest --test-dir build-chronoslob-bridge-onnxrt --output-on-failure
```

Summaries:

- Release configure with Python bindings: passed.
- Release build: passed. First build invocation hit the 120s tool timeout; rerun
  resumed and completed.
- CTest: 1/1 test executable passed.
- Pytest: 85 passed.
- Benchmark target: built.
- Demo: passed; replay, replay parity, diagnostics, audit, portfolio-risk,
  latency-budget and benchmark JSON smoke paths completed.
- ONNX fallback configure/build: passed with ONNX Runtime absent.
- ONNX fallback CTest: 1/1 passed.
- ONNX fallback pytest: 85 passed.
- Real ONNX Runtime compile: passed with the official Windows 1.20.1 archive after
  limiting Ninja parallelism to `-j1`.
- Real ONNX Runtime test execution: blocked before test startup by Windows loader
  error `0xc000007b`; the downloaded runtime depends on missing local API-set DLL
  `api-ms-win-core-path-l1-1-0.dll`.

## Local Benchmark Summary

Command:

```powershell
.\build-chronoslob-bridge\asterion_benchmarks.exe --json build-chronoslob-bridge\chronoslob_bridge_benchmark.json --no-text
```

Default local inference rows, per-call timing in nanoseconds:

| benchmark | backend | model | input | output | avg | p50 | p95 | p99 | p99.9 | max | throughput/s | allocs | bytes |
|---|---|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| feature_extraction_vector_returning | n/a | feature_extractor_v1 | 1x4 | n/a | 269 | 100 | 300 | 400 | 600 | 50300 | 3707796.01 | 200000 | 6400000 |
| feature_extraction_caller_owned_buffer | n/a | feature_extractor_v1_buffer | 1x4 | n/a | 120 | 100 | 100 | 200 | 300 | 21200 | 8269075.72 | 0 | 0 |
| linear_inference_only | linear | linear_w4 | 1x4 | 1x1 | 173 | 100 | 200 | 200 | 300 | 79400 | 5751324.24 | 0 | 0 |
| feature_extraction_plus_linear_vector_returning | linear | linear_w4 | 1x4 | 1x1 | 284 | 100 | 300 | 400 | 700 | 47300 | 3513394.82 | 100000 | 3200000 |
| feature_extraction_plus_linear_caller_owned_buffer | linear | linear_w4 | 1x4 | 1x1 | 151 | 100 | 100 | 200 | 300 | 13900 | 6620806.55 | 0 | 0 |
| measured_linear_inference_only | linear | linear_w4 | 1x4 | 1x1 | 406 | 300 | 600 | 600 | 900 | 146900 | 2462568.95 | 0 | 0 |
| feature_buffer_measured_linear_inference | linear | linear_w4_policy | 1x4 | 1x1 | 344 | 300 | 500 | 600 | 800 | 26400 | 2906385.33 | 0 | 0 |
| inference_policy_overhead | n/a | policy_gate | n/a | n/a | 130 | 100 | 100 | 200 | 400 | 204000 | 7639010.90 | 0 | 0 |
| feature_buffer_policy_gate_overhead | n/a | feature_buffer_policy_gate | 1x4 | n/a | 126 | 100 | 100 | 200 | 400 | 14300 | 7881990.83 | 0 | 0 |

No ONNX Runtime rows were measured locally because the default environment lacked
the dependency, and the downloaded Windows runtime could not be loaded by this
OS image. When ONNX Runtime is usable, the benchmark target emits:

- `chronoslob_onnx_model_load`
- `chronoslob_onnx_inference_only`
- `feature_extraction_plus_chronoslob_onnx_vector_returning`
- `feature_extraction_plus_chronoslob_onnx_caller_owned_buffer`
- `feature_buffer_measured_chronoslob_onnx_inference`

## Timeout And Late-Signal Policy

Timeout and late-signal policy behavior is unchanged. The tests still cover
accepted, timed-out, late-signal and repeated-late disable states with injected
timestamps. The benchmark rows keep policy-gate timing separate from feature
extraction and model scoring.

## Known Limitations

- The ONNX artefact is a deterministic fixture, not a trained ChronosLOB model.
- At the time of this fixture report, ChronosLOB was not modified because the
  local checkout was dirty and no low-risk ONNX export hook existed. The later
  real-model report records the pushed ChronosLOB export helper.
- Real ONNX Runtime scoring was not measured locally because the downloaded
  Windows runtime could not be loaded on this OS image.
- ONNX Runtime remains optional and is not part of default CI dependencies.
- Benchmark numbers are local and machine-dependent.
- The output score is a plumbing score, not a probability, alpha signal or order
  decision.

## Superseded Follow-Up

Completed by the 2026-06-01 real-model bridge pass: a fixed-shape CPU-only
ChronosLOB export helper now exists at pushed commit
`2cf2f32148bc38fb1009f1afaa5cb38deaf1f0b7`, with the default Asterion build
kept dependency-light and ONNX Runtime lanes remaining optional.
