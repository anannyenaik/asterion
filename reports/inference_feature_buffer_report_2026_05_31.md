# Inference Feature Buffer Report - 2026-05-31

These are representative local measurements on this machine/environment, not portable performance claims.

This report covers the caller-owned feature buffer added for inference. It measures plumbing only:
L2 feature extraction, LinearModel scoring and timeout/late-signal policy accounting. It does not
claim predictive quality, trading profitability, production model-serving infrastructure, production
HFT performance or live trading capability.

## Limitation Before This Change

The existing `FeatureExtractor` API returned `std::vector<double>` for L2 features. That was useful
for research, Python bindings and debugging, but it owned dynamic storage in the inference path. The
benchmark row for vector-returning extraction measured one vector allocation per extraction call.

Feature ordering before and after this change is unchanged:

1. `spread_ticks`
2. `mid_price_ticks`
3. `top_level_imbalance`
4. `top_level_quantity`

Feature version remains `1` and feature count remains `4`.

## API Design

The new C++ API is caller-owned:

```cpp
struct FeatureBuffer {
  std::span<double> values;
  std::size_t size;
};

FeatureExtractionStatus FeatureExtractor::extract_into(
    const L2View& view,
    FeatureBuffer& out) const noexcept;
```

`FeatureExtractionStatus::InsufficientCapacity` is returned before writing feature values when the
caller-provided span is too small. The vector-returning `extract(...)` API remains available and now
delegates to `extract_into(...)`, so semantics stay aligned. A templated `extract_from_book_into(...)`
helper lets callers reuse both `L2View` storage and feature storage when the book has been warmed and
the view vectors have been reserved.

## Allocation Accounting

- Vector-returning feature extraction: allocates the returned `std::vector<double>` per call.
- Caller-owned-buffer feature extraction: writes into caller storage and measured 0 allocations after
  warm-up in the scoped unit test and benchmark row.
- LinearModel load/construction: owns model weights and can allocate during construction.
- Steady-state LinearModel scoring: measured 0 allocations after construction.
- Policy gate: measured 0 allocations; it is integer state and injected timing checks.
- Benchmark/reporting: sample vectors and JSON/text formatting are outside the measured loop or
  reported separately by the benchmark runner.
- ONNX model load/scoring: optional, behind `ASTERION_USE_ONNXRUNTIME`; not required by default CI
  and not claimed allocation-free.

## Local Health Gate

Environment:

- OS: Windows
- CPU: Intel64 Family 6 Model 158 Stepping 9, GenuineIntel
- Compiler: GCC 16.1.0 from MSYS2 UCRT64
- CMake: 4.3.3
- Ninja: 1.13.2
- Python used for Release/Python build: 3.14.5 from MSYS2 UCRT64
- Base commit before this change: `1e2cbdbddb69`

Commands run:

```powershell
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; cmake --version
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; ninja --version
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; g++ --version
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; ctest --version
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; .\scripts\configure_release.ps1 -BuildDir build-feature-buffer -PythonExe (Get-Command python).Source
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; cmake --build build-feature-buffer
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; ctest --test-dir build-feature-buffer --output-on-failure
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; $env:PYTHONPATH='C:\Users\Lenovo\Programming\Asterion\build-feature-buffer\python;' + $env:PYTHONPATH; python -m pytest python/tests
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; .\scripts\run_demo.ps1 -BuildDir build-feature-buffer -SkipBuild -PythonExe C:\msys64\ucrt64\bin\python.exe
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; cmake --build build-feature-buffer --target asterion_benchmarks
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; .\build-feature-buffer\asterion_benchmarks.exe --json build-feature-buffer\inference_feature_buffer_benchmark.json --no-text
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; cmake -S . -B build-feature-buffer-default -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_ENABLE_WARNINGS=ON
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; cmake --build build-feature-buffer-default
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; ctest --test-dir build-feature-buffer-default --output-on-failure
```

Summaries:

- Release configure with Python bindings enabled: passed.
- Full build: passed.
- CTest: 1/1 test executable passed.
- Pytest: 83 passed.
- Benchmark target: built.
- Demo: passed; replay parity, audit manifest, portfolio risk, latency-budget JSON and benchmark JSON
  generation all completed.
- Default dependency-light Release build without Python bindings: configured, built and passed CTest.

## Correctness Tests Added

- Vector-returning and caller-owned-buffer extraction produce identical features.
- Repeated extraction is deterministic.
- Insufficient capacity returns `insufficient_capacity`, sets output size to 0 and leaves caller
  storage unchanged.
- Exact feature count and version metadata are asserted.
- Empty and one-sided shallow L2 views produce the existing all-zero feature vector.
- Deep enough L2 views still use deterministic top-of-book semantics.
- Existing vector API still works and delegates to the new path.
- Python-facing behavior remains unchanged; existing Python tests passed.
- Caller-owned feature extraction plus LinearModel scoring has a warmed 0-allocation unit test.

## Measured Benchmark Rows

Benchmark command:

```powershell
$env:PATH='C:\msys64\ucrt64\bin;'+$env:PATH; .\build-feature-buffer\asterion_benchmarks.exe --json build-feature-buffer\inference_feature_buffer_benchmark.json --no-text
```

Per-call instrumented latency distribution in nanoseconds:

| benchmark | backend | model | features | version | iters | p50 | p95 | p99 | p99.9 | max | throughput | allocs | bytes |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| feature_extraction_vector_returning | n/a | feature_extractor_v1 | 4 | 1 | 200000 | 100 | 300 | 400 | 500 | 32100 | 3,934,962.93 | 200000 | 6400000 |
| feature_extraction_caller_owned_buffer | n/a | feature_extractor_v1_buffer | 4 | 1 | 200000 | 100 | 200 | 300 | 300 | 23100 | 6,983,410.91 | 0 | 0 |
| linear_inference_only | linear | linear_w4 | 4 | 1 | 200000 | 100 | 100 | 200 | 300 | 20700 | 7,708,079.61 | 0 | 0 |
| feature_extraction_plus_linear_vector_returning | linear | linear_w4 | 4 | 1 | 100000 | 100 | 300 | 400 | 500 | 33200 | 3,587,276.65 | 100000 | 3200000 |
| feature_extraction_plus_linear_caller_owned_buffer | linear | linear_w4 | 4 | 1 | 100000 | 100 | 200 | 300 | 400 | 14100 | 6,206,669.69 | 0 | 0 |
| measured_linear_inference_only | linear | linear_w4 | 4 | 1 | 50000 | 200 | 300 | 500 | 700 | 14400 | 3,486,385.66 | 0 | 0 |
| feature_buffer_measured_linear_inference | linear | linear_w4_policy | 4 | 1 | 50000 | 200 | 500 | 600 | 900 | 77800 | 3,184,611.96 | 0 | 0 |
| inference_policy_overhead | n/a | policy_gate | n/a | n/a | 200000 | 100 | 100 | 100 | 300 | 48000 | 7,916,841.50 | 0 | 0 |
| feature_buffer_policy_gate_overhead | n/a | feature_buffer_policy_gate | 4 | 1 | 200000 | 100 | 200 | 300 | 300 | 14500 | 6,797,703.74 | 0 | 0 |

Interpretation:

- The old vector-returning extraction rows allocate exactly one 32-byte feature vector per measured
  call in this run.
- The new caller-owned-buffer rows reported 0 measured allocations and 0 allocated bytes.
- LinearModel scoring and the policy gate stayed at 0 measured allocations.
- The p50 values are close to the Windows timer granularity for these tiny operations; treat the
  table as representative local evidence, not portable latency.

## Inference Policy And ONNX Status

Timeout and late-signal behavior remains unchanged. Existing tests still cover accepted, timeout,
late-signal, timeout-and-late-signal and repeated-late disable behavior using injected timestamps.
The buffer path feeds `MeasuredInferenceEngine::score(std::span<const double>)`, so LinearModel
scoring and policy evaluation can consume caller-owned features directly.

ONNX Runtime remains optional. The default build does not require ONNX Runtime. ONNX requests still
fall back to `LinearModel` when the dependency is absent, and the real ONNX benchmark rows are
compiled only when the existing opt-in flag finds the dependency.

## Known Limitations

- The default `OrderBook::l2_view(...)` convenience API still returns owning vectors; use
  `fill_l2_view(...)` with reserved `L2View` storage for allocation-sensitive paths.
- The Python API intentionally stays vector/list oriented for usability; the no-allocation path is a
  C++ hot-path API.
- ONNX Runtime scoring may allocate internally and is not claimed allocation-free.
- These measurements are from a Windows/MSYS2 desktop environment and are not production
  model-serving or HFT latency claims.
- WSL2/Linux perf hardware-counter work remains postponed until reboot or native Linux access.

## Follow-Up

The ChronosLOB-style ONNX bridge follow-up is now documented in
`docs/chronoslob_bridge.md` and
`reports/chronoslob_onnx_bridge_report_2026_05_31.md`. It remains fixture-based
until ChronosLOB has a clean, low-risk ONNX export helper.
