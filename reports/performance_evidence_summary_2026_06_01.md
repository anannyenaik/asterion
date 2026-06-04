# Performance Evidence Summary (2026-06-01)

> **These are representative local measurements on this machine/environment, not
> portable performance claims.** Every number below is copied from an existing
> curated report; nothing here is recomputed, extrapolated or invented. Where a
> metric was never measured, the cell reads `not measured` or `n/a`.

This is a top-level index of the performance and allocation evidence Asterion
already has. It exists so a reviewer can see, in one place, what the benchmarks
prove, what they deliberately do not prove, and which source report backs each
row. For per-claim classification see [docs/claim_audit.md](../docs/claim_audit.md);
for reproduction commands see [docs/evidence_index.md](../docs/evidence_index.md)
and [BENCHMARKS.md](../BENCHMARKS.md).

## What this evidence proves

- The correctness-first hot path (binary replay → L3 book update → reusable L2
  view → fixed-size strategy callback → risk check) runs with a stable,
  reported latency distribution and **deterministic guard/replay checksums** on
  this machine.
- An **opt-in `PooledOrderBook`** reaches **zero measured steady-state
  allocations** after explicit warm-up across the checked-in fixture, eight
  generated stress corpora and seven larger 100k/1M corpora, while matching the
  correctness-first book bit-for-bit (guard/replay checksum parity).
- A **caller-owned feature buffer** removes the one-vector-per-call allocation
  of the convenience feature path (200,000 → 0 measured allocations for that
  row), and `LinearModel` scoring plus the policy gate stay at **0 measured
  allocations** after warm-up.
- The **optional ONNX Runtime backend** loads and scores a real tiny ChronosLOB
  `DeepLOBModel` deterministically, with load cost and per-call allocations
  **measured and reported separately** (no allocation-free claim for ONNX).
- The **optional replay-loop + real ChronosLOB ONNX row** measures the systems
  cost of putting that tiny exported ChronosLOB-style model inside Asterion's
  deterministic replay loop. It is emitted only when the active backend is
  actually ONNX; default builds record it as skipped/unavailable.
- The **opt-in bounded SPSC replay pipeline** preserves single-thread checksums
  exactly under concurrency, with lossless blocking backpressure that genuinely
  saturates on large corpora and **zero dropped events**.
- The recorded **Binance public-depth case study** normalises and replays
  deterministically to fixed correctness checksums.

## What this evidence does not prove

- No portable or cross-machine performance numbers. Every latency/throughput
  figure is from one Windows 10 / MSYS2 laptop and is not comparable elsewhere.
- No production-HFT performance, kernel bypass, FPGA, or colocated networking.
- No live trading, no authenticated exchange/broker connectivity, no order
  placement.
- No profitability, alpha, signal value, or predictive quality — the inference
  path measures *plumbing only*, and the ChronosLOB toy model is trained on
  synthetic toy data with no market meaning.
- No production model-serving claim; ONNX Runtime is opt-in and absent from
  default CI.
- No native Linux `perf` hardware-counter evidence yet (deferred — see
  [Limitations and deferred Linux perf](#limitations-and-deferred-linux-perf)).

## Local environment

All measurements below come from one machine. Individual reports record the exact
benchmark-executable commit field for their run.

| field | value |
| --- | --- |
| OS | Windows 10 10.0.19045, measured through MSYS2/MinGW-w64 UCRT64 |
| CPU | Intel(R) Core(TM) i7-7700HQ CPU @ 2.80GHz, 4 cores / 8 logical processors |
| Benchmark CPU string | Intel64 Family 6 Model 158 Stepping 9, GenuineIntel |
| Compiler | GCC 16.1.0 (MSYS2 UCRT64) |
| Build type | Release (`-O3 -DNDEBUG`) |
| Toolchain | CMake 4.3.3, Ninja 1.13.2 |
| Python (Release/bindings) | 3.14.5 (MSYS2 UCRT64) |
| ONNX Runtime (optional lane) | 1.20.1 (C++), opt-in only |
| Clock | `std::chrono::steady_clock`, ~100 ns resolution on this host |

The ~100 ns clock granularity matters: for sub-microsecond inference operations
the per-call p50 is dominated by timer resolution, not the operation, so
aggregate throughput is the better estimate of raw op cost (see the inference
report).

## Top-level benchmark evidence table

Numbers are transcribed from the source report named in the last column. Latency
is per-call for inference rows and per-event/per-run/aggregate for the replay
rows as noted in each source report.

| area | path measured | dataset / workload | environment | p50 | p99 | p99.9 | throughput | allocations | checksum / parity | source report | caveat |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | --- | --- | --- |
| standard hot path | binary replay → L3 → L2 → strategy → risk | `sample_hot_path_replay.bin` (12 events, 10k iters) | Win/MSYS2 | 800 ns | 2,000 ns | 7,500 ns | 809,066 ev/s | 210,000 | guard `18052214259513584877` | [benchmark_report](benchmark_report_2026_05_31.md) | correctness-first node-based book still allocates; local only |
| pooled hot path | same path via opt-in `PooledOrderBook` | `sample_hot_path_replay.bin` (5k iters) | Win/MSYS2 | 600 ns | 1,300 ns | 6,400 ns | 1,037,075 ev/s | 0 | guard `3714046084935619589` (matches standard "after" row) | [allocation_optimisation](allocation_optimisation_report_2026_05_31.md) | zero-alloc only after warm-up; opt-in |
| pooled stress corpora | standard vs pooled, 8 generated corpora | 4k–8k events each | Win/MSYS2 | not measured (avg/p95 only) | n/a | n/a | see source | std 140,625–259,475 → pooled **0** | guards match: yes (8/8) | [pooled_order_book_stress](pooled_order_book_stress_report_2026_05_31.md) | allocation result is the defensible signal, not latency |
| zero-alloc feature extraction | caller-owned `FeatureBuffer` vs vector-returning | L2 features 1×4, 200k iters | Win/MSYS2 | 100 ns | 300 ns | 300 ns | 6,983,411 op/s | **0** (vs 200,000 for vector path) | unit-tested 0-alloc after warm-up | [inference_feature_buffer](inference_feature_buffer_report_2026_05_31.md) | p50 at timer granularity; plumbing only |
| LinearModel inference | `linear_inference_only` | 1×4 input, 200k iters | Win/MSYS2 | 100 ns | 200 ns | 300 ns | 7,708,080 op/s (aggregate ≈ 5 ns/call, ~190M/s) | 0 | unit-tested 0-alloc | [inference_report](inference_report_2026_05_31.md) | per-call p50 dominated by timer; no predictive claim |
| real ChronosLOB ONNX | `chronoslob_real_onnx_inference_only` | 1×1×4→1×3 DeepLOB, 50k iters | Win/MSYS2 + ONNX RT 1.20.1 | 29.0 µs | 65.7 µs | 100.6 µs | ≈ 33.5k inf/s | ~2 / call (load 20 one-time) | deterministic test vector verified | [chronoslob_real_model_bridge](chronoslob_real_model_bridge_report_2026_06_01.md) | optional ONNX; toy model; not alloc-free; plumbing only |
| event-loop inference | replay hot path + feature + LinearModel + measured policy gate (vs inference-free hot path) | `sample_hot_path_replay.bin` (12 events, 10k iters) | Win/MSYS2 | 1,600 ns (base 800 ns) | 6,300 ns | 28,200 ns | 280,467 ev/s (base 487,639) | 210,000 (= base; inference adds **0**) | guard `17484014929127736293` | [inference_event_loop_cost](inference_event_loop_cost_report_2026_06_01.md) | new run; plumbing only; +~800 ns p50, +0 allocs; tiny 12-event fixture, local only |
| optional event-loop ONNX | replay hot path + feature + real ChronosLOB ONNX + measured policy gate | `sample_hot_path_replay.bin` (12 events, 10k iters) | Win/MSYS2 + ONNX RT 1.20.1 | 61.0 us | 262.2 us | 811.3 us | 13,079 ev/s | 570,000 total (= base book + ~3 ONNX allocs/event) | guard `7611055767038144338` | [inference_event_loop_cost](inference_event_loop_cost_report_2026_06_01.md) | optional ONNX; toy model; plumbing only; not alloc-free; local only |
| SPSC steady-state replay | single-thread vs steady SPSC, `Light` validation | balanced/replace-heavy/deep-book 10k–1M | Win/MSYS2 | n/a (aggregate run) | n/a | n/a | single 0.49M–1.60M ev/s; SPSC 0.37M–1.54M ev/s | dominated by correctness-first book storage | checksum parity: true (6/6); dropped 0 | [spsc_steady_state](spsc_steady_state_report_2026_05_31.md) | absolute numbers dominated by validation cost; ratio is the signal |
| Binance replay case study | normalise → deterministic replay | tiny hand-curated public-depth fixture (11 events) | Win/MSYS2 | not measured | n/a | n/a | not measured | n/a | `final_book_checksum 2539005926052284398`; 0 diagnostics | [binance_replay_case_study](binance_replay_case_study_2026_05_31.md) | correctness checksums, **not** performance; L2→synthetic IDs |
| large generated replay | standard vs pooled hot path | 100k + seven 1M corpora | Win/MSYS2 | std 1,000–1,500 ns; pooled 800–1,200 ns | std 4,100–7,500 ns; pooled 2,800–5,400 ns | see source | std 0.35M–0.87M ev/s; pooled 0.54M–1.16M ev/s | std 4.2M–54M → pooled **0** | guard parity: yes (7/7) | [linux_performance_evaluation](linux_performance_evaluation_2026_05_31.md) | Windows/MSYS2, not native Linux; corpora git-ignored |

## Methodology

The same methodology disciplines run through every report above.

- **Warm-up vs measured iterations.** Each benchmark runs a fixed number of
  warm-up iterations (typically 5 for replay rows) before timing and allocation
  counters are reset, so reported counts are steady-state. The hot-path and
  pooled rows reset counters immediately before the measured loop; ONNX
  model-load cost is measured separately from steady-state inference.
- **Fixed seeds and deterministic corpora.** Generated corpora are produced by
  `scripts/generate_synthetic_events.py` with explicit seeds; the Linux-eval and
  SPSC reports record per-corpus SHA-256 and the exact generation command so the
  *shape* of the workload reproduces even though the timings do not.
- **Checksums and parity checks.** Replay produces deterministic book /
  execution-report / diagnostics / event-log checksums plus a benchmark guard
  checksum. The pooled and SPSC paths are validated by **matching those
  checksums bit-for-bit** against the correctness-first single-thread path, so
  parity is structural rather than coincidental.
- **Allocation counters.** An in-process allocation tracker reports allocation
  counts and bytes after warm-up. Zeros are reported only where a zero is proven
  in a scoped, warmed path; node-based book allocations are reported rather than
  hidden.
- **Validation modes.** Replay defaults to correctness-first `Full` validation
  (full invariant walk after every event). The opt-in `ReplayValidationMode::Light`
  keeps cheap per-event top-of-book checks and defers the full walk to
  end-of-replay; it exists for large-corpus throughput evaluation only and never
  replaces full validation in correctness tests.
- **Local machine dependence.** Every latency/throughput value depends on this
  laptop, its OS scheduling, power state, compiler and timer behaviour. The `max`
  column in several reports is dominated by occasional OS scheduling jitter,
  which is why distributions, not best cases, are reported.
- **Windows/MSYS2 caveat.** All runs are Windows 10 through MSYS2/MinGW-w64
  UCRT64, not native Linux. The compiled Python extension is ABI-specific to the
  interpreter that built it, so the build and the tests/demo must use the same
  Python (see the toolchain note in [docs/evidence_index.md](../docs/evidence_index.md)).
- **HDD/disk-loading caveat.** Large generated corpora (1M-event binaries are
  ~58 MB each) are loaded from local disk before the measured loop; corpus
  loading is outside the measured event loop, but disk and OS cache state are
  part of the local environment and are not portable.
- **Why native Linux perf is pending.** Hardware-counter evidence
  (cycles/instructions/branch/cache, flamegraphs) requires a Linux PMU. On this
  host WSL2 launches but no Linux kernel can boot because hardware virtualization
  is disabled in BIOS/UEFI firmware (`HCS_E_HYPERV_NOT_INSTALLED`;
  `systeminfo` → `Virtualization Enabled In Firmware: No`). This is a firmware
  setting that cannot be changed from software.
- **What Linux perf will add later.** Once a native/cloud Linux host or
  firmware-enabled WSL2 is available, `scripts/run_linux_perf_profile.sh` will
  add `perf stat -d` counters and optional `perf record` + flamegraphs for the
  hot path, turning the current latency distributions into a
  microarchitectural breakdown. The helper fails loudly rather than fabricating
  counters when `perf` is unavailable.
- **Why CI does not gate on benchmark numbers.** Benchmark timings are
  machine-dependent, so CI asserts only deterministic correctness, checksum
  parity and stable allocation behaviour. The benchmark/`linux-performance`
  workflows are manual-dispatch, non-blocking and gate on no numbers.

## Before / after optimisation summary

All rows are local, warmed measurements from the source reports; the allocation
deltas are the defensible result and the latency deltas are local observations.

### Standard `OrderBook` vs opt-in `PooledOrderBook`

`sample_hot_path_replay.bin`, after-change rows from the allocation report
(5,000 iterations); both rows share guard checksum `3714046084935619589`.

| variant | p50 | p99 | throughput | allocations | bytes | parity |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| standard `OrderBook` | 800 ns | 1,900 ns | 852,159 ev/s | 105,000 | 6,720,000 | guard match |
| pooled `PooledOrderBook` | 600 ns | 1,300 ns | 1,037,075 ev/s | **0** | **0** | guard match |

Across the eight generated stress corpora the same pattern holds: standard
allocations of 140,625–259,475 collapse to **0** pooled allocations after
warm-up, with guard-checksum parity on every corpus
([pooled_order_book_stress](pooled_order_book_stress_report_2026_05_31.md)).
Across the seven larger 100k/1M corpora, standard allocations of 4.2M–54M
collapse to **0** pooled, with checksum parity 7/7
([linux_performance_evaluation](linux_performance_evaluation_2026_05_31.md)).

### Vector-returning vs caller-owned feature extraction

L2 features, 200,000 iterations, from the inference reports.

| variant | p50 | p99 | throughput | allocations | bytes |
| --- | ---: | ---: | ---: | ---: | ---: |
| vector-returning (`extract`) | 100 ns | 400 ns | 3,934,963 op/s | 200,000 | 6,400,000 |
| caller-owned buffer (`extract_into`) | 100 ns | 300 ns | 6,983,411 op/s | **0** | **0** |

The features are identical between the two paths (unit-tested), so the
caller-owned path removes one `std::vector<double>` allocation per call with no
change to the feature contract. `LinearModel` scoring and the policy gate stay at
0 allocations on top of either path.

### Per-run SPSC vs steady-state SPSC

The first SPSC pipeline timed `run_spsc_replay` per iteration, so on a 12-event
dataset the per-run number was dominated by thread create/join, not transfer:

| row | p50 (ns/run) | throughput | checksum parity |
| --- | ---: | ---: | --- |
| `replay_l3_diagnostics_single_thread` | 9,600 | ~1,083,091 ev/s | baseline |
| `spsc_replay_l3_diagnostics` (per-run) | 172,500 | ~63,461 ev/s | true |

The steady-state harness creates producer/consumer threads once and times only
the streaming phase, so the SPSC path runs at a comparable fraction of
single-thread throughput while staying lossless and bit-identical:

| corpus | single ev/s | steady SPSC ev/s | ratio | backpressure | dropped | parity |
| --- | ---: | ---: | ---: | ---: | ---: | --- |
| balanced 10k | 1,600,615 | 1,540,168 | 0.962 | 4,184 | 0 | true |
| balanced 100k | 1,343,290 | 1,442,606 | 1.074 | 42,331 | 0 | true |
| replace-heavy 100k | 968,086 | 1,460,669 | 1.509 | 48,482 | 0 | true |
| balanced 1M | 1,063,306 | 1,058,126 | 0.995 | 685,331 | 0 | true |
| replace-heavy 1M | 494,143 | 373,150 | 0.755 | 620,989 | 0 | true |
| deep-book 1M | 820,319 | 824,452 | 1.005 | 497,176 | 0 | true |

Absolute throughput here is dominated by the correctness-first book's
per-event validation cost in *both* paths; the meaningful signals are the
**ratio**, the genuine backpressure saturation, zero drops and exact parity, not
the absolute number (see
[spsc_steady_state](spsc_steady_state_report_2026_05_31.md) and
[spsc_replay_pipeline](spsc_replay_pipeline_report_2026_05_31.md)).

## Limitations and deferred Linux perf

- All numbers are **representative local measurements on one Windows 10 / MSYS2
  laptop**, not portable performance claims.
- The pooled book and the SPSC pipeline are **opt-in**; the correctness-first
  `OrderBook` and single-thread replay remain the defaults and are always
  available.
- Zero-allocation results depend on **explicit warm-up/reservation** in the
  disclosed paths and datasets.
- The ChronosLOB ONNX model is **optional**, trained on **synthetic toy data**,
  and produces a **plumbing-only score** with no predictive meaning; default
  builds and CI never require ONNX Runtime.
- The Binance case study is a **recorded public-depth demo** with synthetic
  order IDs from L2 data — not live trading, not authenticated connectivity, not
  equities-market realism.
- **Native Linux `perf` hardware-counter evidence is deferred.** It is blocked on
  this host by firmware virtualization being disabled in BIOS/UEFI, which
  prevents WSL2 from booting a Linux kernel. The helper script and methodology
  exist; counter values are **not** fabricated while `perf` is unavailable.
  Required to unblock here: enable Intel VT-x / AMD-V in firmware and reboot, or
  run on a native/cloud Linux host where the PMU is exposed. See
  [linux_performance_evaluation](linux_performance_evaluation_2026_05_31.md) and
  [perf_profile.md](perf_profile.md).

## Recommended next work

- **Done (2026-06-01): inference event-loop cost report.** The cost of inserting
  caller-owned feature extraction + `LinearModel` + the measured timeout/late-signal
  policy gate into a full replay event loop is now a dedicated report backed by a new
  measured `hot_path_binary_replay_l3_l2_inference_strategy_risk` benchmark row
  (0 added steady-state allocations vs the inference-free hot path; plumbing only).
  See [inference_event_loop_cost_report_2026_06_01.md](inference_event_loop_cost_report_2026_06_01.md).
- **Done (2026-06-04): optional replay-loop ONNX row.** The real tiny ChronosLOB
  ONNX backend is now measured inside the replay-loop inference pipeline when
  ONNX Runtime is available; default builds report the row as skipped/unavailable.
- A **larger replay-loop inference corpus**: use recorded/simulated data beyond
  the 12-event fixture to observe LinearModel and optional ONNX systems cost
  without adding profitability, alpha or predictive-quality claims.
- The **native Linux perf pass remains last** for performance evidence work; do
  not fabricate hardware-counter values while native Linux/PMU access is absent.
- The **technical paper** remains deferred until native Linux `perf` evidence is
  collected, so the microarchitectural section can be written from measured
  counters rather than placeholders.
</content>
</invoke>
