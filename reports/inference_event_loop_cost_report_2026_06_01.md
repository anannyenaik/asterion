# Inference Event-Loop Cost Report (2026-06-01)

> **These are representative local measurements on this machine/environment, not
> portable performance claims.** This report frames a single question:
> **what is the measured local systems cost of adding inference to a deterministic
> trading event loop?** It measures *plumbing* — feature extraction, model scoring,
> the timeout/late-signal policy gate, fallback behaviour, allocation behaviour and
> the per-event latency impact of inserting inference into replay. It does **not**
> claim model profitability, alpha, signal value, predictive quality for the
> ChronosLOB toy model, production model serving, or production-HFT performance.

This report builds on the existing per-component inference evidence
([inference_report_2026_05_31.md](inference_report_2026_05_31.md),
[inference_feature_buffer_report_2026_05_31.md](inference_feature_buffer_report_2026_05_31.md),
[chronoslob_real_model_bridge_report_2026_06_01.md](chronoslob_real_model_bridge_report_2026_06_01.md))
and adds one **new measured row**: the standard correctness-first hot path with a
research-style inference stage inserted into the event loop, measured back-to-back
against the same hot path without inference on the same run.

## Executive summary

- Adding **synchronous inference** (caller-owned feature extraction → `LinearModel`
  → a measured timeout/late-signal policy gate) into the deterministic replay event
  loop, alongside the existing strategy and risk path, **added zero steady-state
  allocations** on this machine: the inference row and the inference-free row both
  report **210,000 allocations / 13,440,000 bytes** over 120,000 events — i.e. the
  node-based book's `Add`/`Replace` allocations are unchanged and the inference
  stage adds nothing on top.
- On the same run, the per-event latency rose from **p50 800 ns / p99 4,300 ns**
  (no inference) to **p50 1,600 ns / p99 6,300 ns** (with inference), and
  throughput moved from **≈ 488k ev/s** to **≈ 280k ev/s**. The added per-event
  cost is sub-microsecond-to-low-microsecond and is dominated by fixed per-call
  overhead on this tiny 12-event fixture (see [Event-loop impact](#event-loop-impact)).
- The inference-free hot-path row reproduced its previously published guard
  checksum **`18052214259513584877`** exactly, confirming the new benchmark row is
  purely additive and did not perturb the existing path.
- The **optional ONNX path** is 3–4 orders of magnitude more expensive per call
  (real ChronosLOB DeepLOB ≈ **29 µs p50**, ≈ 33.5k inf/s, **~2 allocations/call**
  from ONNX Runtime) and is **not** allocation-free. Those numbers come from the
  existing optional ONNX lane; ONNX Runtime is not installed in this default-build
  run and the full replay-loop-with-ONNX row is **not measured**.
- The model score is folded into the benchmark guard checksum so the stage cannot
  be optimised away, but it **never alters order flow** in this benchmark: no
  decisioning, alpha or profitability behaviour is implied or measured.

## What is measured

- The per-event latency distribution and steady-state allocation count of the
  **full event loop with inference inserted**: binary replay → L3 book update →
  reusable L2 view → caller-owned feature extraction → `LinearModel` scoring →
  `MeasuredInferenceEngine` timeout/late-signal policy accounting, running
  alongside the existing fixed-size strategy callback and pre-trade risk gateway.
- The same event loop **without** inference, on the same run, as the baseline so
  the delta is the added inference cost.
- The isolated per-component costs (feature extraction, `LinearModel` scoring,
  policy-gate accounting), transcribed from the existing curated reports.
- The optional real/fixture ChronosLOB ONNX per-call latency and allocation split,
  transcribed from the existing optional ONNX lane report.

## What is not measured

- **Model quality of any kind** — no predictive value, alpha, signal value or
  profitability. The ChronosLOB toy model is trained on synthetic toy data and the
  Asterion-side score is plumbing only.
- **The full replay event loop with the ONNX backend wired in.** ONNX Runtime is
  opt-in and absent from this default build; the combined "replay loop + ONNX +
  policy" row is marked `not measured`. The optional lane measures ONNX
  *inference-only* and *feature+ONNX+policy* on a static L2 view, not inside replay.
- **Portable or cross-machine latency.** Every nanosecond figure here is from one
  Windows 10 / MSYS2 laptop and is not comparable elsewhere.
- **Native Linux `perf` hardware-counter breakdown.** Deferred — blocked on this
  host by firmware virtualization being disabled in BIOS/UEFI (see
  [Limitations](#limitations)).
- **Asynchronous / off-loop inference**, batching, or any production
  model-serving topology. The measured path is synchronous, in-loop, single model.

## Inference pipeline

The measured event-loop path, in order:

```
market event (recorded binary replay)
  → L3/L2 book update (OrderBook.add/cancel/replace/execute)
  → caller-owned feature extraction (FeatureExtractor::extract_into → FeatureBuffer, 0 alloc)
  → model inference (LinearModel::score, or optional ONNX backend)
  → policy gate (MeasuredInferenceEngine + InferencePolicy: timeout / late-signal accounting)
  → strategy decision or abstain/fallback (ImbalanceStrategy; policy may abstain)
  → risk gateway (RiskGateway::check_new_order)
  → matching / replay accounting (guard + book checksum)
```

In the benchmark, the inference stage is inserted after the reusable L2 view is
built and runs **alongside** the existing strategy + risk path; its output is
consumed into the guard checksum but does not change matching, strategy or risk
behaviour. This isolates the *added systems cost* of inference without making any
decisioning claim.

## LinearModel path

`LinearModel` is the default and fallback backend: a fixed-weight dot product over
the four L2 features plus a bias, returning a single `double`. It is deterministic,
inlined, and **zero-allocation after construction** (asserted by a unit test and
reported as 0 allocations across 200k scores). Aggregate (uninstrumented)
throughput on this host is roughly **5 ns/call (~190M scores/s)**; the per-call
*instrumented* p50 (~100 ns) is dominated by the ~100 ns `steady_clock` resolution,
not by the dot product. This is the correctness-first path that always remains
available regardless of optional dependencies.

## ChronosLOB ONNX path

The optional ONNX backend loads a real tiny ChronosLOB `DeepLOBModel` (CNN-LSTM,
907 parameters, `1×1×4 → 1×3`, trained on **synthetic toy data**) or the legacy
hand-written `Gemm` fixture, behind `-DASTERION_USE_ONNXRUNTIME=ON`. From the
existing optional-lane report (ONNX Runtime 1.20.1, same machine):

- real DeepLOB inference-only ≈ **29.0 µs p50 / 65.7 µs p99**, ≈ **33.5k inf/s**;
- fixture `Gemm` inference-only ≈ **6.9 µs p50**;
- both are 3–4 orders of magnitude above the zero-allocation `LinearModel`;
- steady-state inference allocates **~2 allocations/call (≈ 24 B/call)** from ONNX
  Runtime's per-run buffers — **not allocation-free**;
- one-time model load ≈ **3.33 ms** with ~20 one-time allocations, measured
  separately from steady state.

**This default-build run does not include ONNX Runtime**, so the ONNX rows below
are transcribed from that report and the replay-loop-with-ONNX combination is
`not measured`. When ONNX Runtime is unavailable, an ONNX request deterministically
falls back to `LinearModel` (`fell_back = true`) — see
[Fallback / late-signal behaviour](#fallback--late-signal-behaviour).

## Feature extraction cost

Two feature-extraction modes exist, producing identical features (unit-tested):

- **vector-returning** (`extract`): convenient, but allocates one
  `std::vector<double>` per call (200,000 allocations / 6,400,000 bytes over 200k
  calls in the curated report).
- **caller-owned buffer** (`extract_into` → `FeatureBuffer`): writes into caller
  storage, **0 measured allocations** after warm-up; this is the hot-path API used
  by the event-loop row.

Both report **p50 ≈ 100 ns** (timer-granularity-bound) in the curated report; the
caller-owned path is the one that composes into the zero-allocation event-loop
result below.

## Policy gate and timeout behaviour

The policy gate is implemented by `evaluate_inference_policy` (stateless) and
`InferencePolicyGate` (stateful), with `MeasuredInferenceEngine` wrapping a model +
`InferencePolicy`. The **exact implemented semantics** (from
`cpp/src/inference/inference.cpp`) are:

- `timed_out = (policy.timeout_ns > 0) && (observed_latency_ns > policy.timeout_ns)`.
  The model is scored **first**, then its measured latency is compared to the
  budget — timeout is a *post-hoc accounting/decision*, not a preemption of a
  running inference.
- `late_signal` is evaluated only when `max_signal_age_ns > 0 && signal_ts > 0 &&
  now_ts >= signal_ts`; then `late_signal = (now_ts - signal_ts) > max_signal_age_ns`.
- `accepted = !(timed_out && drop_timed_out) && !(late_signal && drop_late_signals)`.
- `decision ∈ {Accept, Timeout, LateSignal, TimeoutAndLateSignal}`.
- The stateful gate counts **consecutive** late signals; when
  `disable_on_repeated_late_signals` is set and the count reaches
  `max_consecutive_late_signals`, the model latches **disabled** (`model_disabled =
  true`, `accepted = false`) until `reset()`. By default
  (`max_consecutive_late_signals = 0`) the model is never disabled.

The policy gate is **integer state plus injected-timing checks** and is measured at
**0 allocations / ~100 ns p50** in isolation. In the event-loop row the policy is
configured with a 1 ms timeout and no max-signal-age, so the steady-state decision
is `Accept` — i.e. the **synchronous-inference measured baseline**. For a
sub-microsecond `LinearModel`, the latency the timeout compares against is itself
dominated by timer overhead; the timeout is therefore a **reliability mechanism**,
not an alpha mechanism (see [Policy behaviour interpretation](#policy-behaviour-interpretation)).

## Fallback / late-signal behaviour

Fallback is implemented in `make_inference_backend` (`cpp/src/inference/backend.cpp`)
and the returned `InferenceBackendSelection` **always owns a usable `Model`**:

- If ONNX Runtime is not compiled in, or the model cannot be loaded, an ONNX
  request **degrades to `LinearModel`** with `fell_back = true` and a clear
  `detail` diagnostic. Latency on the fallback path is therefore the **LinearModel
  path** above.
- A **feature count / feature version mismatch falls back *before* any model load**,
  so a model whose contract does not match Asterion's L2 feature schema never runs.
- Late-signal handling: per policy, a late signal can be **ignored/abstained**
  (`accepted = false`) or merely flagged, and repeated late signals can **latch the
  model disabled** when configured. These are reliability controls; the no-action
  fallback (abstain) and the LinearModel fallback are both available depending on
  configuration.

The combined "fallback path latency" and "timeout/late-signal decision" are
**behavioural** outcomes asserted by tests, not separate timed rows; they are
marked `not applicable`/`not measured` in the latency table.

## Allocation behaviour

Allocations are reported in disjoint buckets so no number is double-counted or
hidden:

| bucket | source | measured |
| --- | --- | --- |
| **Model load / setup** | `LinearModel` construction; ONNX session/graph build | LinearModel: owns weights at construction, then 0. ONNX: **~20 one-time** allocations at load (≈ 3.33 ms), measured separately |
| **Feature extraction (steady state)** | `extract_into` → caller `FeatureBuffer` | **0** (caller-owned); vector-returning path = 1 `std::vector`/call |
| **Policy-gate (steady state)** | `InferencePolicyGate` / `evaluate_inference_policy` | **0** (integer state, injected-timing checks) |
| **LinearModel inference (steady state)** | `LinearModel::score` | **0** after construction |
| **ONNX inference (steady state)** | ONNX Runtime per-run buffers | **~2 allocations/call (≈ 24 B/call)** — **not** allocation-free |
| **Event loop, node-based book** | `OrderBook` `Add`/`Replace` nodes | 210,000 / 120k events on this fixture — present with or without inference |

Claim boundaries preserved:

- The **caller-owned `FeatureBuffer` + `LinearModel` + policy-gate** stage is
  zero-allocation **where measured**: the event-loop row's allocation count equals
  the inference-free row's count (both 210,000), so the inference stage added
  **zero** allocations on top of the book.
- The **ONNX path is not described as allocation-free**; ONNX Runtime still
  allocates ~2 allocations/call in steady state.
- **Model-load allocations are separated** from steady-state inference allocations.

## Event-loop impact

New measurement, this report. Default Release build (no ONNX Runtime),
i7-7700HQ-class CPU / Windows 10 / GCC 16.1.0 (`-O3 -DNDEBUG`), benchmark commit
field `a644e14f683f`, dataset `sample_hot_path_replay.bin` (12 events),
10,000 iterations × 12 events = 120,000 events, 5 warm-up iterations, both rows
measured back-to-back in the same process:

| row | p50 | p95 | p99 | p99.9 | max | throughput | allocations | bytes | guard |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `hot_path_binary_replay_l3_l2_strategy_risk` (no inference) | 800 ns | 3,600 ns | 4,300 ns | 12,800 ns | 2,336,100 ns | 487,639 ev/s | 210,000 | 13,440,000 | `18052214259513584877` |
| `hot_path_binary_replay_l3_l2_inference_strategy_risk` (**+ feature + LinearModel + policy**) | 1,600 ns | 5,400 ns | 6,300 ns | 28,200 ns | 12,950,900 ns | 280,467 ev/s | 210,000 | 13,440,000 | `17484014929127736293` |

Reading these honestly:

- **Allocation delta = 0.** The inference stage is caller-owned and the measured
  engine's per-call strings stay in small-string-optimised storage, so the row's
  allocation count is identical to the inference-free row.
- **Latency delta ≈ +800 ns p50 (~2×)** on this fixture, throughput ≈ 488k → 280k
  ev/s. The added per-event cost is **larger than the standalone sub-microsecond
  component rows** because (a) `MeasuredInferenceEngine` performs two extra
  `steady_clock::now()` reads per event to time the model, which on a ~100 ns-clock
  is a real repeated cost, and (b) the loop re-extracts features from a **freshly
  rebuilt** L2 view each event rather than a static warm view. The base row's tail
  and throughput also differ from the previously published hot-path run (same p50
  800 ns, same guard, different absolute throughput), which is exactly why these
  are labelled representative-local, not portable.
- The base row's guard `18052214259513584877` **matches the value already
  published** in
  [performance_evidence_summary_2026_06_01.md](performance_evidence_summary_2026_06_01.md),
  confirming the new row did not change the existing path.
- This is a **tiny 12-event fixture** with a sub-microsecond hot path, so the
  inference cost is dominated by fixed per-call overhead; do **not** extrapolate the
  ~2× factor to a larger book, a richer feature set, or another machine.

## Measured evidence table

Columns: path · feature-extraction mode · model/backend · policy behaviour ·
p50 · p95 · p99 · p99.9 · max · throughput · allocations · source · caveat.
`n/m` = not measured, `n/a` = not applicable. Per-call rows are timer-granularity
bound at p50; per-event rows are whole-event latency.

| path | feat. mode | model/backend | policy | p50 | p95 | p99 | p99.9 | max | throughput | allocs | source | caveat |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| feature extraction only | vector-returning | n/a | n/a | 100 ns | 300 ns | 400 ns | 500 ns | 32,100 ns | 3,934,963 op/s | 200,000 | [inference_feature_buffer](inference_feature_buffer_report_2026_05_31.md) | allocates 1 vector/call; plumbing only |
| feature extraction only | caller-owned `FeatureBuffer` | n/a | n/a | 100 ns | 200 ns | 300 ns | 300 ns | 23,100 ns | 6,983,411 op/s | **0** | [inference_feature_buffer](inference_feature_buffer_report_2026_05_31.md) | p50 at timer granularity |
| feature + model | caller-owned `FeatureBuffer` | LinearModel | none | 100 ns | 200 ns | 300 ns | 400 ns | 14,100 ns | 6,206,670 op/s | **0** | [inference_report](inference_report_2026_05_31.md) | no predictive claim |
| model only | n/a | LinearModel | none | 100 ns | 100 ns | 200 ns | 300 ns | 20,700 ns | 7,708,080 op/s (agg ≈ 5 ns/call) | **0** | [inference_report](inference_report_2026_05_31.md) | per-call p50 timer-bound |
| model + policy gate | caller-owned `FeatureBuffer` | LinearModel | Accept (timeout 1 ms) | 200 ns | 500 ns | 600 ns | 900 ns | 77,800 ns | 3,184,612 op/s | **0** | [inference_report](inference_report_2026_05_31.md) | measured engine adds 2 clock reads/call |
| policy gate only | n/a | n/a | injected timings | 100 ns | 100 ns | 100 ns | 300 ns | 48,000 ns | 7,916,842 op/s | **0** | [inference_report](inference_report_2026_05_31.md) | integer state; reliability control |
| ONNX inference only | n/a | real ChronosLOB DeepLOB | none | 29.0 µs | 43.9 µs | 65.7 µs | 100.6 µs | 305 µs | ≈ 33.5k inf/s | ~2/call | [chronoslob_real](chronoslob_real_model_bridge_report_2026_06_01.md) | **optional**; toy model; not alloc-free |
| feature + ONNX + policy | caller-owned `FeatureBuffer` | real ChronosLOB DeepLOB | Accept | 28.2 µs | 44.9 µs | 66.1 µs | n/m | 341 µs | n/m | ~2/call | [chronoslob_real](chronoslob_real_model_bridge_report_2026_06_01.md) | **optional**; static L2 view, not replay loop |
| **full event loop, no inference** | n/a | n/a | n/a | **800 ns** | 3,600 ns | 4,300 ns | 12,800 ns | 2,336,100 ns | 487,639 ev/s | 210,000 | **this report (new run)** | node-based book allocs; local only |
| **full event loop + inference** | caller-owned `FeatureBuffer` | LinearModel | Accept (timeout 1 ms) | **1,600 ns** | 5,400 ns | 6,300 ns | 28,200 ns | 12,950,900 ns | 280,467 ev/s | 210,000 | **this report (new run)** | +0 allocs vs base; +~800 ns p50; local only |
| full event loop + ONNX | caller-owned `FeatureBuffer` | ONNX | Accept | n/m | n/m | n/m | n/m | n/m | n/m | n/m | n/a | **not measured**: ONNX Runtime not in default build |
| fallback path (ONNX→LinearModel) | caller-owned `FeatureBuffer` | LinearModel (`fell_back=true`) | n/a | = LinearModel path | n/a | n/a | n/a | n/a | = LinearModel path | **0** | `tests/unit/test_inference_backend.cpp` | behavioural; latency = LinearModel |
| timeout / late-signal decision | n/a | any | Timeout / LateSignal / disable | n/a | n/a | n/a | n/a | n/a | n/a | **0** | `tests/unit/test_telemetry_inference.cpp` | behavioural; reliability, not alpha |

## Policy behaviour interpretation

How the inference policy should be read (documenting only what is implemented):

- **Synchronous inference is allowed as a measured baseline.** The event-loop row
  scores the model in-line and accepts the result; this is the documented baseline,
  not a recommendation for production topology.
- **Late signals can be abstained from or ignored depending on policy.** With
  `drop_late_signals = true` a late signal yields `accepted = false` (abstain);
  with it false the signal is flagged but used. The stateful gate can additionally
  latch the model disabled after `max_consecutive_late_signals` consecutive late
  signals when `disable_on_repeated_late_signals` is set.
- **Fallback can use LinearModel or no-action behaviour depending on
  configuration.** An unavailable/invalid ONNX model falls back to `LinearModel`;
  an abstaining policy decision yields no action. Both are available.
- **Confidence/timeout thresholds are system controls, not profitability claims.**
  `timeout_ns` and `max_signal_age_ns` bound *reliability* (how stale or slow a
  signal may be before it is dropped). They say nothing about whether a signal is
  *good*.
- **Timeout handling is a reliability mechanism, not an alpha mechanism.** The model
  is scored first and the latency compared after; for a sub-µs `LinearModel` the
  compared latency is mostly timer overhead, so the timeout exists to shed slow/late
  signals, never to imply predictive value.

## Limitations

- All numbers are **representative local measurements on one Windows 10 / MSYS2
  laptop**, not portable performance claims; absolute latency/throughput vary
  run-to-run with OS scheduling and timer behaviour.
- The hot path measured is a **tiny 12-event fixture** with a sub-microsecond base
  cost; the inference impact is dominated by fixed per-call overhead and must not be
  extrapolated to larger books, richer features or other machines.
- The **ChronosLOB toy model is trained on synthetic toy data**; the Asterion-side
  score is **plumbing only** with no predictive or market meaning.
- **ONNX Runtime is optional** and absent from this default build/CI; the full
  replay-loop-with-ONNX row is `not measured`, and ONNX inference is **not**
  allocation-free.
- This is **not** live trading, authenticated exchange/broker connectivity, order
  placement, production model serving or production-HFT infrastructure.
- **Native Linux `perf` hardware-counter evidence is deferred**, blocked on this
  host by firmware virtualization disabled in BIOS/UEFI (WSL2 cannot boot a Linux
  kernel; `HCS_E_HYPERV_NOT_INSTALLED`). Counter values are not fabricated while
  `perf` is unavailable. The correctness-first `LinearModel`/`OrderBook` path
  remains the default and is always available.

## Recommended next work

- A **larger recorded public market-data case study** (still public, still recorded,
  still no profitability/realism claim) so the event-loop inference cost can be
  observed over a richer normalise→replay corpus rather than the 12-event fixture.
- An **opt-in replay-loop-with-ONNX row** behind `ASTERION_HAVE_ONNXRUNTIME`, to
  measure the full event loop with the real ChronosLOB backend (currently only the
  static-view ONNX rows exist).
- The **technical paper remains deferred** until native Linux `perf` evidence is
  collected, so the microarchitectural section can be written from measured counters
  rather than placeholders.

## Reproduce

Default dependency-light lane (no ONNX Runtime, no Python required):

```powershell
$env:Path = 'C:\msys64\ucrt64\bin;' + $env:Path
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_ENABLE_WARNINGS=ON
cmake --build build --target asterion_benchmarks
.\build\asterion_benchmarks.exe --json build\event_loop_bench.json --hot-path-iterations 10000
```

The new row is `hot_path_binary_replay_l3_l2_inference_strategy_risk` (category
`inference`); compare it to the core `hot_path_binary_replay_l3_l2_strategy_risk`
row from the same run. Generated benchmark JSON is git-ignored and not committed.
The optional ONNX rows are produced only in the opt-in ONNX Runtime build; see
[chronoslob_real_model_bridge_report_2026_06_01.md](chronoslob_real_model_bridge_report_2026_06_01.md).
