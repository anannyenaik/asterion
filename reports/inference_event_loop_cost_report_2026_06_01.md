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
and adds measured event-loop rows for the standard `LinearModel` path and, when
ONNX Runtime is available, the real tiny ChronosLOB ONNX backend inside the same
deterministic replay-loop inference pipeline.

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
- The **optional replay-loop + ChronosLOB ONNX path** is now measured in an
  ONNX-enabled build as systems-cost evidence for putting a tiny exported
  ChronosLOB-style model into Asterion's deterministic replay loop. The row
  `hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk`
  reported **61.0 us p50 / 262.2 us p99 / 811.3 us p99.9**, **~13.1k ev/s** and
  **570,000 allocations / 19,440,000 bytes** over 120,000 replay events on this
  local run. This is optional ONNX Runtime plumbing evidence only.
- The ONNX replay-loop row is emitted only when the active backend is actually
  ONNX. In the default no-ONNX build it is recorded under `skipped_benchmarks`
  with an unavailable reason; it does not silently fall back to `LinearModel` and
  count those timings as ONNX evidence.
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
- The optional **full replay event loop with the real ChronosLOB ONNX backend** in
  an ONNX-enabled local build: replay -> L3/L2 book update -> caller-owned feature
  extraction -> real tiny ChronosLOB ONNX scoring -> measured policy gate ->
  strategy/risk/replay accounting.

## What is not measured

- **Model quality of any kind** — no predictive value, alpha, signal value or
  profitability. The ChronosLOB toy model is trained on synthetic toy data and the
  Asterion-side score is plumbing only.
- **Default-build ONNX replay-loop timing.** ONNX Runtime is opt-in and absent
  from default builds/CI; in that lane the combined "replay loop + ONNX + policy"
  row is reported under `skipped_benchmarks`, not as a numeric benchmark row.
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

This update adds one more optional row in an ONNX-enabled build:
`hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk`.
It keeps model load/session setup outside the steady-state replay timer, warms
the path, then measures per-event replay + L3/L2 update + caller-owned feature
extraction + real ChronosLOB ONNX scoring + policy gate + strategy/risk/replay
accounting. The model-load row remains separate (`chronoslob_real_onnx_model_load`:
about 6.6 ms and 20 one-time allocations in this run).

When ONNX Runtime is unavailable, an ONNX request deterministically falls back to
`LinearModel` (`fell_back = true`) for normal backend selection, but the ONNX
benchmark rows require an active ONNX backend. If fallback occurs, the replay-loop
ONNX benchmark is skipped/reported unavailable and is not counted as ONNX evidence.
See [Fallback / late-signal behaviour](#fallback--late-signal-behaviour).

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
| **ONNX inference (steady state)** | ONNX Runtime per-run buffers and input/output staging | inference-only **~2 allocations/call**; policy/replay rows showed **~3 allocations/event** on this run — **not** allocation-free |
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

## Optional ONNX replay-loop evidence

Optional ONNX Runtime evidence for the systems cost of putting a tiny exported
ChronosLOB-style model into Asterion's deterministic replay loop was collected in
an ONNX-enabled Release build. This is not model profitability evidence, alpha
evidence, predictive-quality evidence, production model serving, production-HFT
performance, or portable latency evidence.

Environment: Windows 10/MSYS2 UCRT64, Intel i7-7700HQ-class CPU string reported
by the benchmark as `Intel64 Family 6 Model 158 Stepping 9, GenuineIntel`,
GCC 16.1.0, Release (`-O3 -DNDEBUG`), ONNX Runtime 1.20.1 C++ runtime from
`build-chronoslob-onnxruntime/onnxruntime`, dataset
`sample_hot_path_replay.bin` (12 events), 10,000 iterations x 12 events =
120,000 measured events, 5 warm-up iterations, benchmark JSON
`build-onnxrt/onnx_replay_loop_benchmark_2026_06_04.json` (generated, ignored).

Model-load/setup is separated from steady state. `chronoslob_real_onnx_model_load`
reported about **6.6 ms total**, **6.0 ms sampled p50**, **20 one-time
allocations** and **2,230 bytes** in this process. The replay-loop row constructs
the ONNX selection before warm-up and resets allocation counters only after
warm-up, so the per-event numbers below are steady-state replay-loop costs, not
session creation costs.

| row | backend | p50 | p95 | p99 | p99.9 | max | throughput | allocations | bytes | guard |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| `hot_path_binary_replay_l3_l2_strategy_risk` | n/a | 3.0 us | 4.3 us | 5.4 us | 75.8 us | 2.824 ms | 213,462 ev/s | 210,000 | 13,440,000 | `18052214259513584877` |
| `hot_path_binary_replay_l3_l2_inference_strategy_risk` | LinearModel | 1.3 us | 1.7 us | 2.3 us | 40.7 us | 15.973 ms | 529,457 ev/s | 210,000 | 13,440,000 | `17484014929127736293` |
| `hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk` | ONNX real ChronosLOB | **61.0 us** | **202.5 us** | **262.2 us** | **811.3 us** | **21.170 ms** | **13,079 ev/s** | **570,000** | **19,440,000** | `7611055767038144338` |

Reading this honestly:

- The ONNX replay-loop row measured replay + L3/L2 update + caller-owned feature
  extraction + real tiny ChronosLOB ONNX scoring + measured policy gate +
  strategy/risk/replay accounting. The ONNX score is folded into the guard and
  does not alter order flow.
- Allocation split: the same-process no-inference and LinearModel replay rows
  both reported **210,000 allocations / 13,440,000 bytes**, all from the
  correctness-first node-based book path on this fixture. The ONNX replay row
  reported **570,000 allocations / 19,440,000 bytes**, so the ONNX steady-state
  stage added **360,000 allocations / 6,000,000 bytes** over 120,000 events
  (**3 allocations/event, 50 bytes/event**) on this run. ONNX is therefore not
  allocation-free; model-load allocations remain separate.
- Same-process baseline timings were noisy (the core hot-path p50 was slower than
  the LinearModel inference row in this particular run). The robust signals are
  the emitted ONNX row, its backend/model tags, the separated load-vs-steady-state
  allocation counts and the explicit local environment, not any portable latency
  ratio.
- In the default no-ONNX build, the same row is not emitted as a numeric
  benchmark. It appears in `skipped_benchmarks` with reason
  `onnx runtime not compiled in...`, preserving the dependency-light default path.

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
| **full event loop + ONNX** | caller-owned `FeatureBuffer` | real ChronosLOB DeepLOB | Accept | **61.0 us** | 202.5 us | 262.2 us | 811.3 us | 21.170 ms | 13,079 ev/s | 570,000 | **this report (optional ONNX run)** | ONNX Runtime opt-in; +3 allocs/event over base; local only; plumbing only |
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
- **ONNX Runtime is optional** and absent from default build/CI. The full
  replay-loop-with-ONNX row is measured only in an opt-in ONNX Runtime build; the
  default build records it as skipped/unavailable, and ONNX inference is **not**
  allocation-free.
- This is **not** live trading, authenticated exchange/broker connectivity, order
  placement, production model serving or production-HFT infrastructure.
- **Native Linux `perf` hardware-counter evidence is deferred**, blocked on this
  host by firmware virtualization disabled in BIOS/UEFI (WSL2 cannot boot a Linux
  kernel; `HCS_E_HYPERV_NOT_INSTALLED`). Counter values are not fabricated while
  `perf` is unavailable. The correctness-first `LinearModel`/`OrderBook` path
  remains the default and is always available.

## Recommended next work

- A **larger replay-loop inference corpus** (still recorded/simulated, still no
  profitability or predictive-quality claim) so LinearModel and optional ONNX
  event-loop costs can be observed beyond the 12-event fixture.
- The **native Linux perf pass remains last** for performance evidence work; do
  not fabricate hardware-counter values while native Linux/PMU access is absent.
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

The LinearModel event-loop row is `hot_path_binary_replay_l3_l2_inference_strategy_risk`
(category `inference`); compare it to the core
`hot_path_binary_replay_l3_l2_strategy_risk` row from the same run. In default
builds, the ONNX replay-loop row appears only under `skipped_benchmarks` because
ONNX Runtime is not compiled in. Generated benchmark JSON is git-ignored and not
committed.

Optional ONNX Runtime lane used for the measured ONNX replay-loop row:

```powershell
$ort = (Resolve-Path 'build-chronoslob-onnxruntime\onnxruntime').Path
$env:Path = (Join-Path $ort 'lib') + ';C:\msys64\ucrt64\bin;' + $env:Path
cmake -S . -B build-onnxrt -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DASTERION_USE_ONNXRUNTIME=ON "-DONNXRUNTIME_ROOT=$ort"
cmake --build build-onnxrt --target asterion_tests asterion_benchmarks
Copy-Item (Join-Path $ort 'lib\onnxruntime.dll') build-onnxrt -Force
Copy-Item (Join-Path $ort 'lib\onnxruntime_providers_shared.dll') build-onnxrt -Force
ctest --test-dir build-onnxrt --output-on-failure
.\build-onnxrt\asterion_benchmarks.exe --json build-onnxrt\onnx_replay_loop_benchmark.json `
  --no-text --hot-path-iterations 10000
```

The ONNX replay-loop row is
`hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk`. The
older static-view ONNX rows remain documented in
[chronoslob_real_model_bridge_report_2026_06_01.md](chronoslob_real_model_bridge_report_2026_06_01.md).
