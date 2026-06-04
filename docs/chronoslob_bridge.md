# ChronosLOB ONNX Bridge

This bridge is scoped as research-model-to-systems integration using a tiny
ChronosLOB-style ONNX artefact. It is model-plumbing evidence only.

It does not claim predictive quality, trading profitability, live trading,
production model serving, production HFT, SOTA modelling or a portable latency
guarantee.

There are now **two** checked-in artefacts:

1. a hand-written deterministic **fixture** (`Gemm` linear head), and
2. a **real, tiny ChronosLOB model** (a trained `DeepLOBModel`) exported from
   ChronosLOB code.

## Artefacts

Fixture (hand-written, deterministic):

- Model: `data/models/chronoslob_tiny_fixture.onnx` (564 bytes)
- Metadata: `data/models/chronoslob_tiny_fixture.metadata.json`
- Export helper: `tools/export_chronoslob_tiny_onnx.py`
- Report: `reports/chronoslob_onnx_bridge_report_2026_05_31.md`

The fixture is a 564-byte deterministic artefact, not a trained ChronosLOB
checkpoint. It uses a fixed `Gemm` node as a small linear scoring head over the
Asterion L2 feature buffer.

Real ChronosLOB model (trained, exported from ChronosLOB):

- Model: `data/models/chronoslob_tiny_real.onnx` (7158 bytes)
- Metadata: `data/models/chronoslob_tiny_real.metadata.json`
- Export script: ChronosLOB `tools/export_tiny_asterion_onnx.py`
- Report: `reports/chronoslob_real_model_bridge_report_2026_06_01.md`

The real artefact is a genuinely trained `DeepLOBModel` (CNN-LSTM, 907
parameters) from ChronosLOB commit
`2cf2f32148bc38fb1009f1afaa5cb38deaf1f0b7`, trained on **synthetic toy data**
and exported to ONNX (opset 17). It is a systems-integration / inference-latency
artefact only: no predictive-quality, profitability, live-trading or
production-serving claim. See the real-model report for the full contract,
training summary, measured latency/allocation split and reproduction commands.

## ChronosLOB Source Status

The real artefact was exported from the pushed ChronosLOB source at
`https://github.com/anannyenaik/chronos-lob`, commit
`2cf2f32148bc38fb1009f1afaa5cb38deaf1f0b7`. The export metadata records
`source_dirty=false`, and the commit contains the ChronosLOB-side export helper
(`tools/export_tiny_asterion_onnx.py`) plus its reproduction note
(`docs/asterion_export.md`).

The older deterministic fixture remains an Asterion-side compatibility fixture;
its original fixture-only source status is preserved in
`reports/chronoslob_onnx_bridge_report_2026_05_31.md`.

## Model Contract

| field | value |
|---|---|
| model name | `chronoslob_tiny_fixture` |
| input name | `features` |
| input shape | `1x4` |
| output name | `score` |
| output shape | `1x1` |
| feature count | `4` |
| feature version | `1` |
| trained model | `false` |
| deterministic fixture | `true` |

Feature order:

1. `spread_ticks`
2. `mid_price_ticks`
3. `top_level_imbalance`
4. `top_level_quantity`

Expected deterministic check:

- input: `[2.0, 1000.0, 0.5, 400.0]`
- output: `[-0.08499999999999998]`

The output is a single deterministic plumbing score. It is not a class
probability, alpha signal or trading decision.

## Feature Compatibility

`load_model_metadata(...)` reads the fixture metadata, and
`validate_feature_compatibility(...)` checks the model feature count/version
against Asterion's current L2 feature schema before ONNX selection is trusted.

If a requested ONNX model declares a mismatched feature count or feature version,
`make_inference_backend(...)` falls back to `LinearModel` and records a clear
diagnostic such as `feature count mismatch`.

The caller-owned feature buffer remains the hot-path API:

```cpp
std::array<double, asterion::kL2FeatureCount> storage{};
asterion::FeatureBuffer buffer{storage};
extractor.extract_into(view, buffer);
selection.model->score(buffer.used());
```

The vector-returning convenience API is preserved and continues to use the same
feature order.

## Backend Behaviour

Default builds do not require ONNX Runtime. `LinearModel` remains the default
backend and the fallback. The optional ONNX backend is compiled only when
`-DASTERION_USE_ONNXRUNTIME=ON` is requested and ONNX Runtime is found.

When ONNX Runtime is unavailable, ONNX requests return a usable `LinearModel`
selection with `fell_back=true`. When ONNX Runtime is available, the backend
loads `chronoslob_tiny_fixture.onnx`, exposes model name/input shape/output shape
through diagnostics, and validates the fixed input size before scoring.

Model-load allocations are separated from steady-state inference allocations in
the optional ONNX benchmark rows. ONNX inference is measured and reported; it is
not claimed allocation-free.

## Benchmark Rows

Default benchmark rows include:

- `linear_inference_only`
- `feature_extraction_caller_owned_buffer`
- `feature_extraction_plus_linear_caller_owned_buffer`
- `measured_linear_inference_only`
- `feature_buffer_measured_linear_inference`
- `inference_policy_overhead`
- `feature_buffer_policy_gate_overhead`

When built with a real ONNX Runtime, the benchmark target also emits a suite for
each artefact, labelled `chronoslob_fixture` and `chronoslob_real`:

- `<label>_onnx_model_load`
- `<label>_onnx_inference_only`
- `feature_extraction_plus_<label>_onnx_vector_returning`
- `feature_extraction_plus_<label>_onnx_caller_owned_buffer`
- `feature_buffer_measured_<label>_onnx_inference`
- `hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk`
  for the real ChronosLOB artefact only. This optional row measures replay ->
  L3/L2 update -> caller-owned feature extraction -> real tiny ChronosLOB ONNX
  scoring -> measured policy gate -> strategy/risk/replay accounting. It is
  emitted only when the active backend is actually ONNX; default builds report it
  as skipped/unavailable rather than timing the `LinearModel` fallback under an
  ONNX name.

The `chronoslob_real` rows exercise the trained `DeepLOBModel`; the
`chronoslob_fixture` rows exercise the legacy `Gemm` fixture for comparison.

Each inference row reports p50, p95, p99, p99.9, max, throughput, allocation
count, allocation bytes, backend name, model name, input shape, output shape,
feature count and feature version.

## Reproduction

Real ChronosLOB artefact export / verify (from Asterion, with a clean or
throwaway ChronosLOB checkout and Python dependencies for Torch, ONNX and ONNX
Runtime available):

```powershell
git -C ..\ChronosLOB\chronos-lob fetch origin main
git -C ..\ChronosLOB\chronos-lob checkout --detach 2cf2f32148bc38fb1009f1afaa5cb38deaf1f0b7
python ..\ChronosLOB\chronos-lob\tools\export_tiny_asterion_onnx.py --verify `
  --output data\models\chronoslob_tiny_real.onnx `
  --metadata-output data\models\chronoslob_tiny_real.metadata.json
```

Fixture regenerate / verify:

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
python tools\export_chronoslob_tiny_onnx.py --verify
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_ENABLE_WARNINGS=ON -DASTERION_BUILD_PYTHON=ON
cmake --build build
ctest --test-dir build --output-on-failure
$env:PYTHONPATH=(Resolve-Path 'build\python').Path
python -m pytest python\tests
.\build\asterion_benchmarks.exe --json build\chronoslob_bridge_benchmark.json --no-text
```

Optional ONNX Runtime build:

```bash
cmake -S . -B build-onnxrt -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DASTERION_ENABLE_WARNINGS=ON \
  -DASTERION_BUILD_PYTHON=ON \
  -DASTERION_USE_ONNXRUNTIME=ON \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake --build build-onnxrt
ctest --test-dir build-onnxrt --output-on-failure
./build-onnxrt/asterion_benchmarks --json build-onnxrt/chronoslob_bridge_benchmark.json --no-text
```

These are representative local measurements on this machine/environment, not
portable performance claims.
