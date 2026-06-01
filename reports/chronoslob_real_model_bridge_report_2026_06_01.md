# ChronosLOB Real-Model Bridge Report (2026-06-01)

This report upgrades the ChronosLOB integration from a hand-written deterministic
ONNX fixture to a **real, tiny ChronosLOB model artefact** exported from
ChronosLOB code, loaded through Asterion's optional ONNX Runtime backend, with
the deterministic `LinearModel` fallback preserved.

**Scope / claim boundary.** This is *a tiny ChronosLOB research-model artefact
exported into Asterion for systems-integration and inference-latency
evaluation.* It does **not** claim predictive quality, trading profitability,
live trading, production model-serving, production HFT, SOTA modelling, or a
portable latency guarantee.

> These are representative local measurements on this machine/environment, not
> portable performance claims.

## ChronosLOB source

| field | value |
|---|---|
| repo | `https://github.com/anannyenaik/chronos-lob` |
| local path | `../ChronosLOB/chronos-lob` |
| source commit | `2cf2f32148bc38fb1009f1afaa5cb38deaf1f0b7` |
| working tree at export | clean (`source_dirty=false`) |
| export script | `tools/export_tiny_asterion_onnx.py` (committed in ChronosLOB) |
| export doc | `docs/asterion_export.md` (committed in ChronosLOB) |

The ChronosLOB commit adds only the export script and its doc; the export uses
ChronosLOB's own `chronoslob.models.DeepLOBModel` (`create_deeplob_model`).

## Artefact: trained, not a fixture

| field | value |
|---|---|
| artefact | `data/models/chronoslob_tiny_real.onnx` (7158 bytes) |
| metadata | `data/models/chronoslob_tiny_real.metadata.json` |
| `trained_model` | **true** |
| `artefact_type` | `trained_synthetic_smoke` |
| model class | `DeepLOBModel` (CNN-LSTM), 907 parameters |
| framework | PyTorch 2.12.0+cpu → ONNX (opset 17, IR 8) |
| `onnx_sha256` | `8e4241db6d8cc16eb843e2179aaee6baf620e709160d189560e9f17bf789f98b` |

This is a genuinely trained network, not the legacy hand-written fixture. It is
**not** untrained: the smoke run reduces the loss and reaches high accuracy on
its synthetic toy task (below). The legacy fixture remains checked in and is
still exercised for comparison.

### Training (synthetic toy smoke run)

| field | value |
|---|---|
| data | seeded synthetic toy, 512 samples, standardised O(1) 4-feature rows |
| label rule | artificial 3-class (`down/flat/up`) driven by imbalance + spread + noise |
| seed | 7 |
| optimiser / steps | Adam, lr 0.03, 400 full-batch steps, cross-entropy |
| initial train loss | 1.030692 |
| eval loss | 0.123732 |
| eval accuracy | 0.947266 |

The toy features are positionally aligned to Asterion's L2 feature ordering but
hold standardised synthetic values, **not** real tick/quantity scales and **not**
FI-2010 or any market data. The learned relationship is an artificial synthetic
rule with no market meaning.

## Model contract and feature mapping

| field | value |
|---|---|
| input | `features`, shape `1x1x4` |
| output | `logits`, shape `1x3` (raw `[down, flat, up]` logits) |
| feature count / version | `4` / `1` |
| feature order | `spread_ticks, mid_price_ticks, top_level_imbalance, top_level_quantity` |

Asterion's four caller-owned L2 features map **1:1, in order**, to the model's
single-timestep `[1, 1, 4]` input — `shape_value_count(input_shape) == 4 ==
kL2FeatureCount`, so the existing caller-owned feature buffer feeds the model
with no adapter and no change to the zero-allocation feature path. `lookback ==
1` is a deliberate simplification of DeepLOB's multi-row window.

Because the model was trained on a standardised synthetic toy distribution
(not Asterion's real feature scale), feeding Asterion's live L2 features yields a
deterministic **plumbing score only**, with no predictive meaning. Asterion
consumes `output[0]` (the first/`down` logit) as that scalar plumbing score.

### Deterministic test vector

* input: `[0.5, -0.25, 0.8, 0.2]`
* output (ONNX Runtime): `[-5.758237838745117, -2.2456984519958496, 7.596829891204834]`
* `expected_test_output_engine`: `onnxruntime` (1.20.1, same family Asterion links)

`tools/export_tiny_asterion_onnx.py --verify` confirms the committed artefact
reproduces this vector via ONNX Runtime (`max_abs_diff = 0.00e+00`). On this
machine, retraining is byte-identical (the regenerated ONNX matched the
committed SHA256), but the committed artefact — not bit-exact regeneration — is
the determinism contract.

## ONNX Runtime status and fallback

* Default Asterion build does **not** require ONNX Runtime; CI stays
  dependency-light and network-free. `LinearModel` is the default and fallback.
* The optional backend compiles only with `-DASTERION_USE_ONNXRUNTIME=ON` plus a
  discoverable ONNX Runtime (here the local C++ ONNX Runtime 1.20.1).
* When ONNX Runtime is unavailable, an ONNX request returns a usable
  `LinearModel` selection with `fell_back=true` and a clear diagnostic
  (`onnx runtime not compiled in ...`).
* A feature-count / feature-version mismatch falls back *before* any model load
  with a `feature count mismatch` / `feature version mismatch` diagnostic.
* When ONNX Runtime is available, Asterion loads `chronoslob_tiny_real.onnx`,
  exposes `backend=onnx`, `input_shape=1x1x4`, `output_shape=1x3`, and verifies
  deterministic scoring against the recorded expected output.

Metadata validation (`validate_model_metadata`) adds, beyond the existing
checks: an *unsupported model shape* error when the input shape's value count
does not match `feature_count` (or has a non-fixed dimension), an output-shape
fixed-dimension check, and an `artefact_type` / `trained_model` consistency
check. `load_model_metadata` reads `model_class` and `artefact_type` and treats
`reference_weights` as optional (a real exported model carries none).

## Measured results (local, representative)

Toolchain: Windows 10, MinGW UCRT64 GCC, Release, ONNX Runtime 1.20.1 (C++),
single process. Steady-state rows are 50,000 iterations after a warm-up.

### Latency

| row | timing | p50 | p95 | p99 | p99.9 | max |
|---|---|---|---|---|---|---|
| `linear_inference_only` | per-call | ~0 ns | 100 ns | 100 ns | — | 20.7 µs |
| `chronoslob_fixture_onnx_inference_only` | per-call | 6.9 µs | 8.8 µs | 23.3 µs | — | 251 µs |
| `chronoslob_real_onnx_inference_only` | per-call | 29.0 µs | 43.9 µs | 65.7 µs | 100.6 µs | 305 µs |
| `feature_buffer_measured_chronoslob_real_onnx_inference` (policy gate) | per-call | 28.2 µs | 44.9 µs | 66.1 µs | — | 341 µs |
| `chronoslob_real_onnx_model_load` | model-load | 3.33 ms | — | — | — | 3.33 ms |

The real DeepLOB CNN-LSTM is ~4× the trivial fixture `Gemm` per inference
(~29 µs vs ~7 µs), and both are ~3–4 orders of magnitude above the
zero-allocation `LinearModel`. Real-model throughput ≈ 33.5k inferences/s.

**Model-load timing is one-time and order-sensitive.** The first ONNX session
created in the process pays ONNX Runtime's process-wide initialisation; in this
run the fixture suite ran first (its load ≈ 30 ms includes that one-time cost)
and the real-model load ran second (≈ 3.33 ms, incremental session creation).
Load is reported separately from steady-state inference and is not folded into
per-call latency.

### Allocation split (load vs steady-state)

| row | allocations | per call | bytes |
|---|---|---|---|
| `chronoslob_real_onnx_model_load` | 20 (one-time) | — | — |
| `chronoslob_real_onnx_inference_only` | 100,000 / 50k | 2 | 24 B/call |
| `feature_extraction_plus_chronoslob_real_onnx_caller_owned_buffer` | 100,000 / 50k | 2 | — |
| `feature_extraction_plus_chronoslob_real_onnx_vector_returning` | 150,000 / 50k | 3 | — |

Model-load allocations (session/graph setup, ~20) are measured separately from
steady-state inference. Steady-state inference shows ~2 allocations/call, all
from ONNX Runtime's per-run buffers — the caller-owned feature-extraction path
adds **zero** (the caller-owned-buffer row has the same allocation count as
inference-only, while the vector-returning row adds exactly one allocation/call
for the returned `std::vector`). **No allocation-free claim is made for ONNX
inference; the counts are measured and reported.** The `LinearModel` hot path
remains zero-allocation.

### Late-signal / fallback policy status

The policy-gated row (`feature_buffer_measured_chronoslob_real_onnx_inference`)
drives the real ONNX model through `MeasuredInferenceEngine` /
`InferencePolicy`. With a generous timeout the decisions are `Accept` (no
timeouts, no late signals); the C++ test
`ONNX Runtime loads the real ChronosLOB DeepLOB artefact and scores
deterministically` asserts `decision == Accept` and a deterministic score. The
timeout / late-signal / abstention machinery is unchanged from the existing
inference policy and is not re-litigated here.

A standalone strategy-replay demo wiring the real ONNX backend into the
`asterion_replay` event stream was **not** added this pass (it is optional and
would require threading the optional backend + a model-path argument through the
replay tool behind `#if ASTERION_HAVE_ONNXRUNTIME`). The policy-gated real-ONNX
path is already exercised by the benchmark row above and the
`MeasuredInferenceEngine` test, so no PnL, profitability or predictive claim is
made or implied.

## Tests

C++ (`tests/unit/test_inference_backend.cpp`), default lane (no ONNX Runtime)
and ONNX lane:

* real metadata loads and validates (`trained_model`, `artefact_type`,
  `model_class`, `1x1x4`/`1x3` shapes, feature count/version, empty
  `reference_weights`, `shape_value_count == 4`);
* `unsupported model shape` rejection (input shape ≠ feature_count);
* `artefact_type` / `trained_model` mismatch rejection;
* real-model ONNX request with a mismatched feature count falls back clearly
  (deterministic regardless of ONNX Runtime);
* ONNX lane: real DeepLOB loads, scores `expected_test_output[0]`
  deterministically (first == second), drives `MeasuredInferenceEngine`
  (`backend=onnx`, `model_name=chronoslob_tiny_real.onnx`, `Accept`);
* ONNX lane: real-model load vs steady-state allocations measured separately.

Python (`python/tests/test_chronoslob_bridge.py`): real metadata is explicit and
honest, artefact is tiny (< 50 KB), and `onnx_sha256` matches the file.

Default lane: **CTest 1/1 passed, pytest passed.** ONNX lane: **175 test cases,
114,982 assertions passed.**

## Limitations

* Trained on synthetic toy data only; **no** predictive-quality, profitability,
  alpha or generalisation claim.
* Reduced 4-feature, single-timestep (`lookback==1`) simplification of DeepLOB;
  the toy feature scale deliberately differs from Asterion's real feature scale,
  so the Asterion-side score is plumbing only.
* Not live trading infrastructure, not production model-serving, not
  production-HFT infrastructure.
* Latency/allocation numbers are local and representative, not portable.
* ONNX Runtime is opt-in; default builds and CI never require it.

## Reproduction

Export / verify (ChronosLOB, with `[torch]` + `onnx` + `onnxruntime`):

```bash
python tools/export_tiny_asterion_onnx.py \
  --output ../Asterion/data/models/chronoslob_tiny_real.onnx \
  --metadata-output ../Asterion/data/models/chronoslob_tiny_real.metadata.json
python tools/export_tiny_asterion_onnx.py --verify \
  --output ../Asterion/data/models/chronoslob_tiny_real.onnx \
  --metadata-output ../Asterion/data/models/chronoslob_tiny_real.metadata.json
```

Default Asterion lane (no ONNX Runtime):

```powershell
$env:PATH='C:\msys64\ucrt64\bin;' + $env:PATH
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_ENABLE_WARNINGS=ON -DASTERION_BUILD_PYTHON=ON
cmake --build build
ctest --test-dir build --output-on-failure
$env:PYTHONPATH=(Resolve-Path 'build\python').Path
python -m pytest python\tests
```

Optional ONNX Runtime lane:

```powershell
$ort='C:\Users\Lenovo\Programming\Asterion\build-chronoslob-onnxruntime\onnxruntime'
$env:PATH=(Join-Path $ort 'lib') + ';C:\msys64\ucrt64\bin;' + $env:PATH
cmake -S . -B build-onnxrt -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_ENABLE_WARNINGS=ON `
  -DASTERION_USE_ONNXRUNTIME=ON "-DONNXRUNTIME_ROOT=$ort"
cmake --build build-onnxrt --target asterion_tests asterion_benchmarks
# ONNX Runtime DLLs must be loadable by the exe (copy next to it or keep on PATH):
Copy-Item (Join-Path $ort 'lib\onnxruntime.dll') build-onnxrt
Copy-Item (Join-Path $ort 'lib\onnxruntime_providers_shared.dll') build-onnxrt
ctest --test-dir build-onnxrt --output-on-failure
.\build-onnxrt\asterion_benchmarks.exe --json build-onnxrt\onnx_bench.json --no-text
```
