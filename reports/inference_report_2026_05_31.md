# Inference Report - 2026-05-31

These are representative local measurements on this machine/environment, not portable performance claims.

This report measures the *plumbing cost* of the inference path: feature extraction, model scoring
and the timeout/late-signal policy gate. It does **not** claim any predictive quality, signal value,
profitability or live-trading performance. The numbers below were produced on a single Windows
laptop and are not comparable across machines.

## Scope

Measured paths (all reported under the benchmark runner's separate `inference` category, kept apart
from the replay/book/matching/risk "core" timings):

- vector-returning feature extraction (`feature_extraction_vector_returning`)
- caller-owned-buffer feature extraction (`feature_extraction_caller_owned_buffer`)
- LinearModel inference only (`linear_inference_only`)
- vector-returning feature extraction + LinearModel
  (`feature_extraction_plus_linear_vector_returning`)
- caller-owned-buffer feature extraction + LinearModel
  (`feature_extraction_plus_linear_caller_owned_buffer`)
- measured-engine path (model score + policy accounting) (`measured_linear_inference_only`)
- caller-owned-buffer feature extraction + measured LinearModel/policy path
  (`feature_buffer_measured_linear_inference`)
- event-loop policy-gate overhead, injected timings (`inference_policy_overhead`)
- caller-owned-buffer feature extraction + policy-gate overhead
  (`feature_buffer_policy_gate_overhead`)
- ONNX inference only — **only when built with ONNX Runtime** (`onnx_inference_only`)
- feature extraction + ONNX — **only when built with ONNX Runtime**
  (`feature_extraction_plus_onnx_inference`)
- caller-owned-buffer feature extraction + ONNX (only when built with ONNX Runtime)
  (`feature_extraction_plus_onnx_caller_owned_buffer`)

## Environment

- Source commit measured by the benchmark executable: `1e2cbdbddb69`
- OS: Windows
- CPU: Intel64 Family 6 Model 158 Stepping 9, GenuineIntel
- Compiler: GCC 16.1.0 from MSYS2 UCRT64
- Build type: Release
- Compiler flags reported by CMake: `-O3 -DNDEBUG`
- Toolchain: CMake 4.3.3, Ninja 1.13.2
- ONNX Runtime: **not installed locally**; this run measured the deterministic LinearModel and the
  fallback/feature paths only. ONNX rows are produced only in the opt-in ONNX Runtime build/CI lane.

## Command

```powershell
$env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path
.\build-feature-buffer\asterion_benchmarks.exe --json build-feature-buffer\inference_feature_buffer_benchmark.json --no-text
```

## Method And Honesty Notes

- The inference benchmarks use **per-call instrumentation**: each iteration is timed individually so
  a real p50/p95/p99/p99.9/max distribution can be reported (`timing_mode = per-call`).
- On this machine `std::chrono::steady_clock` has a resolution of roughly **100 ns**. For the
  sub-microsecond LinearModel and policy-gate operations the per-call **p50 is therefore dominated by
  timer granularity, not by the operation itself**. The aggregate (uninstrumented) throughput is a
  better estimate of raw per-operation cost: an aggregate run of `linear_inference_only` on this
  machine measured roughly **5 ns/call** (~190M scores/s), versus the ~145 ns/call *instrumented*
  average dominated by two clock reads per iteration.
- Allocation counts are measured with the in-process allocation tracker after a warm-up and are
  reported, not asserted to be zero where a zero is not proven.
- Replay/book/matching/risk timings are reported separately (see
  [benchmark_report_2026_05_31.md](benchmark_report_2026_05_31.md)) and are never folded into these
  inference numbers.

## Measured Results (this machine, this run)

Per-call instrumented latency distribution (nanoseconds) and steady-state allocations:

| benchmark | backend | model | input | iters | avg | p50 | p95 | p99 | p99.9 | max | throughput (op/s) | allocs | bytes |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| feature_extraction_vector_returning | n/a | feature_extractor_v1 | 1x4 | 200000 | 254 | 100 | 300 | 400 | 500 | 32100 | 3,934,963 | 200000 | 6400000 |
| feature_extraction_caller_owned_buffer | n/a | feature_extractor_v1_buffer | 1x4 | 200000 | 143 | 100 | 200 | 300 | 300 | 23100 | 6,983,411 | 0 | 0 |
| linear_inference_only | linear | linear_w4 | 1x4 | 200000 | 129 | 100 | 100 | 200 | 300 | 20700 | 7,708,080 | 0 | 0 |
| feature_extraction_plus_linear_vector_returning | linear | linear_w4 | 1x4 | 100000 | 278 | 100 | 300 | 400 | 500 | 33200 | 3,587,277 | 100000 | 3200000 |
| feature_extraction_plus_linear_caller_owned_buffer | linear | linear_w4 | 1x4 | 100000 | 161 | 100 | 200 | 300 | 400 | 14100 | 6,206,670 | 0 | 0 |
| measured_linear_inference_only | linear | linear_w4 | 1x4 | 50000 | 286 | 200 | 300 | 500 | 700 | 14400 | 3,486,386 | 0 | 0 |
| feature_buffer_measured_linear_inference | linear | linear_w4_policy | 1x4 | 50000 | 314 | 200 | 500 | 600 | 900 | 77800 | 3,184,612 | 0 | 0 |
| inference_policy_overhead | n/a | policy_gate | n/a | 200000 | 126 | 100 | 100 | 100 | 300 | 48000 | 7,916,842 | 0 | 0 |
| feature_buffer_policy_gate_overhead | n/a | feature_buffer_policy_gate | 1x4 | 200000 | 147 | 100 | 200 | 300 | 300 | 14500 | 6,797,704 | 0 | 0 |

Notes on the numbers above:

- **LinearModel scoring allocates nothing** after construction (0 allocations across 200k scores);
  this is also asserted by a unit test.
- **The policy gate allocates nothing** and adds only the cost of integer comparisons; its disable
  latch is checked with injected timings.
- **The vector-returning feature extraction path allocates one `std::vector<double>` per call**
  (200,000 allocations over 200,000 calls). This is kept for convenience and reported honestly.
- **The caller-owned-buffer feature extraction path reported 0 allocations and 0 bytes** in the
  scoped warmed benchmark rows and has a deterministic unit allocation check.
- The old/new feature extraction + LinearModel rows separate the allocation-owning convenience path
  from the caller-owned path.
- The `max` column is dominated by occasional OS scheduling jitter (a single ~100 µs sample), which
  is expected on a non-isolated desktop OS and is why distributions, not best cases, are reported.

## LinearModel vs ONNX (same machine only)

A LinearModel-vs-ONNX comparison is meaningful **only on the same machine and build**. ONNX Runtime
is not installed in this local environment, so this run could not measure the ONNX rows; it measured
the deterministic LinearModel path and confirmed the documented fallback. The benchmark runner emits
`onnx_inference_only`, `feature_extraction_plus_onnx_inference` and
`feature_extraction_plus_onnx_caller_owned_buffer` rows automatically when the binary is built with
`-DASTERION_USE_ONNXRUNTIME=ON` and a real ONNX Runtime is found (see the opt-in `onnx-runtime` CI
lane). When those rows are present:

- the tiny checked-in identity fixture (`data/fixtures/identity_1x4.onnx.b64`, 141 bytes) is decoded
  to a temp file, scored, and removed;
- the identity model returns its first input feature, so scoring is deterministic and is asserted in
  the ONNX unit test;
- ONNX model-load allocations are measured separately from steady-state inference allocations, and
  ONNX inference is **not** claimed to be allocation-free.

Because the identity fixture does no arithmetic, any ONNX-vs-LinearModel latency gap on a given
machine reflects ONNX Runtime call/session overhead versus an inlined dot product — it is a plumbing
comparison, not a model-quality comparison.

## What This Report Does Not Claim

- No predictive quality, alpha, signal value or trading profitability.
- No portable or cross-machine performance numbers.
- No production HFT or live-trading performance.
- No claim that ONNX inference is allocation-free.

## Reproduce

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asterion_benchmarks
./build/asterion_benchmarks --json build/inference_report_run.json
# Opt-in ONNX rows:
cmake -S . -B build-onnx -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_ONNXRUNTIME=ON \
  -DONNXRUNTIME_ROOT=/path/to/onnxruntime
cmake --build build-onnx --target asterion_benchmarks
./build-onnx/asterion_benchmarks --json build-onnx/inference_report_run.json
```

The ONNX fixture itself can be regenerated and verified with:

```bash
python scripts/generate_onnx_fixture.py --verify
```
