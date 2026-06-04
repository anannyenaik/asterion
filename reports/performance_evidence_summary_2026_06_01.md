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

**Reading order.** The **primary** performance context is now the Durham
Hamilton8 HPC compute-node pass (Section: *Primary performance evidence*). The
older Lenovo Windows/MSYS2 laptop and WSL2 laptop passes are retained as
**historical / local development** baselines (Section: *Historical / local
development evidence*); they are not silently replaced or relabelled, but they
are no longer the headline performance context for the paths Durham measured.
Cross-machine comparison of these numbers is not meaningful.

## Primary performance evidence: Durham Hamilton8 HPC compute-node pass

The primary performance context is the 2026-06-04 Durham University Hamilton8 HPC
Slurm compute-node pass in
[durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md).
It is the only pass run on a non-WSL Linux compute node (Rocky Linux 8.10, AMD
EPYC 7702, GCC 13.2 Release, one shared Slurm allocation on
`cn025.ham8.dur.ac.uk`, source commit `216f473`). It supersedes the older
laptop/WSL context **for the paths it actually measured** — the 1M
high-cancellation standard-vs-pooled hot path, the small stress
standard-vs-pooled hot path, the six 1M steady-state SPSC corpora, the
balanced-10k LinearModel replay-loop inference cost, and the explicit Linux
`perf stat` counters / completed `perf record` hotspots. It does **not** recompute
every older row; rows it did not measure (see the comprehensive table below)
remain laptop/WSL evidence.

It is still a **representative shared-HPC measurement**, not a portable or
production-HFT claim: one shared compute-node allocation, no LLC events, no root
control over governor/turbo, GCC evidence only, the optional ONNX backend not
built (ONNX replay-loop row skipped), and one inference `perf report --stdio`
conversion OOM-killed (that hotspot report is incomplete and not interpreted).

### Durham Hamilton8 primary evidence table

Numbers transcribed from
[durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md);
nothing is recomputed or invented. Environment: Rocky Linux 8.10, AMD EPYC 7702,
GCC 13.2 Release, one Slurm allocation, CPU affinity 121.

| path measured | dataset / workload | p50 (std / pooled or base / loop) | p99 | throughput | allocations | checksum / parity | caveat |
| --- | --- | ---: | ---: | ---: | ---: | --- | --- |
| std vs pooled hot path | `high_cancellation_1m` (1M, 5 iters) | 281 ns / 291 ns | 631 ns / 681 ns | 2.88M / 2.82M ev/s | std 7,340,580 → pooled **0** | guard `14180005740461440914` (match) | allocation removal under parity is the signal, not the latency tie |
| std vs pooled stress hot path | 8 small stress corpora (20k–40k ev each) | 180–491 ns std / pooled | 311–882 ns | see source | std 28,125–51,895 → pooled **0** | guard parity 8/8 | single-symbol book layer; `stress_multi_symbol_style` skipped |
| 1M steady-state SPSC | six 1M corpora, `Light` validation | n/a (aggregate) | n/a | single 1.01M–3.75M / SPSC 0.82M–4.63M ev/s | node-book storage 1.47M–2.55M/run (not pooled) | parity true 6/6; dropped **0**; max depth 4096 | bounded queue saturates; ratio is the signal |
| LinearModel inference replay-loop | `balanced_10k` (500k events) | base 371 ns / loop 511 ns | base 601 / loop 792 ns | base 2.13M / loop 1.65M ev/s | base 703,450; inference adds **0** | guard `1442857765714779360` | +≈140 ns p50; plumbing only; ONNX row skipped (not built) |
| `perf stat` counters | whole benchmark processes | n/a | n/a | IPC 0.91–2.02; cache-miss 5.45–43.71% of refs; branch-miss ≤1.20% | n/a | n/a | LLC events unsupported; some grouped events multiplexed |
| `perf record` hotspots | high-cancellation hot path + baseline SPSC | n/a | n/a | top: `append_to_checksum`, `fnv1a_append_byte`, `operator new`, hashtable find/insert | n/a | n/a | `-O3` partial call graphs; inference hotspot OOM-killed, not used |

## Historical / local development evidence

The rows below are retained as **historical / local development baselines** from
**one Lenovo i7-7700HQ laptop**. They are not superseded line-for-line by Durham
— only the specific paths Durham re-ran on Hamilton8 (above) have a newer primary
context. They are kept because they are the original optimisation evidence and
because several rows (the Windows/MSYS2 inference, ONNX bridge and Binance
case-study rows) were never re-run on Hamilton at all.

- **Lenovo Windows 10 / MSYS2 laptop pass** — the original hot-path, allocation,
  pooled-stress, inference, feature-buffer, ONNX bridge, SPSC and Binance
  case-study reports. These are the bulk of the comprehensive table below and the
  only source for the ONNX/inference-cost and Binance rows.
- **WSL2 laptop pass** — the same laptop under WSL2 (Ubuntu 24.04.4, kernel
  `6.6.114.1-microsoft-standard-WSL2`, GCC 13.3.0 Release, virtualized PMU). It
  was the first Linux `perf` evidence and predates Hamilton8. It is a virtualized
  PMU with no LLC events and uncontrolled turbo — kept as the laptop-Linux
  baseline, not a primary context.

### Was every old result recomputed on Hamilton?

**No.** Only the documented Durham paths above were measured on Hamilton8. The
older laptop/WSL rows are retained verbatim as historical/local development
evidence; their numbers are unchanged and clearly attributed to their own
environments in the comprehensive table.

## What Hamilton measured

- GCC Release configure/build and `ctest` (100% passed) on a Rocky Linux compute
  node.
- 1M high-cancellation standard-vs-pooled hot path: pooled reached **0 measured
  allocations** with guard-checksum parity against the standard book.
- Eight small stress corpora standard-vs-pooled: pooled **0 allocations** with
  guard parity 8/8.
- Six 1M steady-state SPSC corpora: lossless (**0 dropped**), checksum parity
  6/6, bounded queue saturated at depth 4096.
- Balanced-10k LinearModel replay-loop inference: **+≈140 ns p50, 0 added
  allocations** over the inference-free node book.
- Explicit Linux `perf stat` counters (cycles/instructions/branch/cache/L1) and
  completed `perf record` hotspots for the high-cancellation hot path and
  baseline SPSC process.

## What Hamilton did not measure

- No portable or cross-node/cross-cluster claim — one shared Slurm allocation.
- No LLC cache events (unsupported for this user allocation); some grouped
  `perf stat -d` events were multiplexed, so explicit event lists are used.
- No root control over CPU governor/turbo, NMI watchdog or PMU scheduling.
- No Clang evidence accepted (one allocation-tracker assertion failed; later
  Clang benchmark attempts missed `libc++.so.1`).
- No optional ONNX Runtime backend (not built; ONNX replay-loop row skipped, not
  timed through a fallback).
- The balanced-10k inference `perf report --stdio` conversion was OOM-killed, so
  that one hotspot report is incomplete and is not interpreted.
- No Windows/MSYS2-only rows (the convenience ONNX bridge, larger Binance
  case study, etc.) were re-run on Hamilton.

## Why old laptop/WSL rows are retained

- They are the **original optimisation evidence** (before/after allocation
  story) and document methodology that the Hamilton pass reuses.
- Several rows — the ONNX ChronosLOB bridge, the inference event-loop ONNX row,
  the Binance public-depth case study — were **never measured on Hamilton** and
  exist only as laptop evidence.
- Deleting them would hide history; relabelling them as Hamilton results would be
  dishonest. They stay, clearly attributed to their own Windows/MSYS2 or WSL2
  environment, with the explicit note that cross-machine comparison is not
  meaningful.

## Claim boundaries (apply to every row above and below)

- All numbers are **representative measurements on the stated host** (Durham
  Hamilton8 shared HPC node, Windows/MSYS2 laptop, or WSL2 laptop), **not
  portable** and **not production-HFT** performance claims.
- No live trading, no authenticated exchange/broker connectivity, no order
  placement.
- No profitability, alpha, signal value or predictive-quality claim — the
  inference path measures **plumbing only**, and the ChronosLOB toy model is
  trained on synthetic toy data with no market meaning.
- No production model-serving claim; ONNX Runtime is opt-in and absent from
  default CI and from the Durham pass.
- Binance data remains **public crypto L2 only** with synthetic order IDs.
- The correctness-first `OrderBook` and single-thread replay remain the
  **defaults**; the pooled book, SPSC pipeline and `Light` validation are opt-in.

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
- The **Durham Hamilton8 HPC pass** shows the same scoped allocation/parity
  story on a Slurm compute node: GCC Release `ctest` passed, the 1M
  high-cancellation pooled hot path reached **0 measured allocations** with
  guard parity, all six 1M SPSC rows were lossless with checksum parity, and
  explicit Linux `perf` counters were collected under disclosed PMU limits.

## What this evidence does not prove

- No portable or cross-machine performance numbers. Most curated rows are from
  one Windows 10 / MSYS2 laptop; the WSL2 and Durham HPC rows are explicitly
  labelled with their own environments. None is comparable across machines.
- No production-HFT performance, kernel bypass, FPGA, or colocated networking.
- No live trading, no authenticated exchange/broker connectivity, no order
  placement.
- No profitability, alpha, signal value, or predictive quality — the inference
  path measures *plumbing only*, and the ChronosLOB toy model is trained on
  synthetic toy data with no market meaning.
- No production model-serving claim; ONNX Runtime is opt-in and absent from
  default CI.
- No portable Linux `perf` evidence. Hardware-counter evidence has now been
  collected both in WSL2 and on Durham Hamilton8 HPC, but both are disclosed
  representative environments, not portable latency/counter claims. The WSL2
  pass uses a virtualized PMU on one laptop; the Durham pass uses one shared
  Slurm allocation with no LLC events and no root control over governor/turbo.

## Local environment

Most rows below come from one laptop. Those rows are the Windows 10 / MSYS2
environment in the table below; the **WSL2 Linux** rows in the evidence table
(and the [2026-06-01 Linux report](linux_performance_evaluation_2026_06_01.md))
were measured on the **same laptop under WSL2** (Ubuntu 24.04.4, kernel
`6.6.114.1-microsoft-standard-WSL2`, GCC 13.3.0 Release, perf 6.8.12 on a
virtualized PMU). The **Durham HPC** rows are from a separate Hamilton8 Slurm
compute-node allocation documented in
[durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md).
Individual reports record the exact benchmark-executable commit field for their
run.

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

## Comprehensive cross-report evidence table

This is the full cross-report table covering **every** environment. The Durham
Hamilton8 rows summarised in the primary table above appear here in their full
form (last row); the remaining rows are the **historical / local development**
Windows/MSYS2 and WSL2 laptop evidence, kept verbatim and attributed to their own
environment in the `environment` column. Cross-machine comparison of these
numbers is not meaningful.

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
| **WSL2 Linux** std vs pooled hot path + perf counters | standard vs pooled L3 hot path | `high_cancellation_1m` (1M, 5 iters) | **WSL2** | std/pooled 300 ns | std/pooled 800 ns | std 12,900 / pooled 13,500 ns | std 2.66M / pooled 2.71M ev/s | std 7,340,580 → pooled **0** | guard `14180005740461440914` (match) | [linux_performance_evaluation_2026_06_01](linux_performance_evaluation_2026_06_01.md) | WSL2 virtualized PMU (no LLC, multiplexed); uncontrolled turbo; not native/cloud Linux; not portable |
| **WSL2 Linux** SPSC steady-state | single-thread vs SPSC, `Light` validation | six 1M corpora | **WSL2** | n/a (aggregate) | n/a | n/a | single 0.77M–3.37M ev/s; SPSC 0.56M–1.24M ev/s | node-book storage (not pooled): 1.47M–2.55M/run | checksum parity true (6/6); dropped 0 | [linux_performance_evaluation_2026_06_01](linux_performance_evaluation_2026_06_01.md) | ratio is the signal; `Light` is a throughput mode, not correctness |
| **WSL2 Linux** inference replay-loop | LinearModel feature+score+policy gate vs inference-free hot path | `balanced_10k` (500k events) | **WSL2** | base 400 / loop 500 ns | base 900 / loop 1,200 ns | base 1,700 / loop 1,800 ns | base 1.61M / loop 1.27M ev/s | base 703,450; inference adds **0** | guard `1442857765714779360` | [linux_performance_evaluation_2026_06_01](linux_performance_evaluation_2026_06_01.md) | plumbing only; +~100 ns p50; ONNX row skipped (not built on Linux); WSL2 local |
| **WSL2 Linux** perf-stat counters | steady-state Light replay process | `baseline_1m`/`replace_heavy_1m`/`deep_book_1m` (1M) | **WSL2** | n/a | n/a | n/a | IPC 0.73–0.74; 2.5–2.9 GHz | branch-miss <1.4%; cache-miss 66–69% of refs; L1-dcache-miss 5.4–7.6% | n/a | [perf_profile](perf_profile.md) | virtualized PMU, LLC `<not supported>`, counters multiplexed (49–75%); pinned `taskset -c 2` |
| **Durham HPC Linux** hot path / SPSC / inference / perf counters | GCC Release on Hamilton8 Slurm compute node | 1M hot path, six 1M SPSC corpora, `balanced_10k` inference | **Rocky Linux HPC** | hot std 281 ns / pooled 291 ns; inference base 371 ns / loop 511 ns | hot std 631 ns / pooled 681 ns; inference base 601 ns / loop 792 ns | hot std 812 ns / pooled 832 ns; inference base 831 ns / loop 1,002 ns | hot std 2.88M / pooled 2.82M ev/s; SPSC 0.90M–4.63M ev/s | pooled hot path **0**; inference adds **0** over node book | hot guard parity `14180005740461440914`; SPSC parity true 6/6 | [durham_hpc_performance_evaluation_2026_06_04](durham_hpc_performance_evaluation_2026_06_04.md) | single shared Slurm allocation; GCC only; no LLC events; governor/turbo uncontrolled; one inference hotspot report OOM-killed |

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
- **Linux perf is now collected, in WSL2.** The earlier firmware-virtualization
  blocker is resolved; WSL2 boots, the project builds and `ctest` passes, and the
  (virtualized) PMU exposes hardware counters. `perf stat -d` cycles/instructions/
  IPC/branch/cache counters and `perf record` hotspots were captured around the
  steady-state replay and hot-path workloads. This is **WSL2, one laptop**: the
  virtualized PMU has no `LLC` cache events, counters are time-multiplexed, and
  CPU turbo is uncontrolled — representative WSL2 measurements, not native/cloud
  Linux and not portable. See
  [linux_performance_evaluation_2026_06_01.md](linux_performance_evaluation_2026_06_01.md).
- **Durham HPC Linux perf is also collected.** The 2026-06-04 Hamilton8 pass ran
  on a Rocky Linux Slurm compute node, not a login node. GCC Release built and
  `ctest` passed, large deterministic corpora were generated under `/nobackup`,
  and explicit `perf stat` event lists counted cycles/instructions/branch/cache/L1
  events around selected benchmark processes. This is **one shared HPC allocation**:
  no LLC events, no root control over governor/turbo, GCC evidence only, and one
  inference `perf report --stdio` conversion was OOM-killed. See
  [durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md).
- **What a more controlled Linux pass would still add.** A host/allocation with
  `LLC` cache events, fixed governor/turbo and a frame-pointer profiling build
  would improve counter completeness and flamegraph-quality call graphs. The
  helper `scripts/run_linux_perf_profile.sh` fails loudly rather than fabricating
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

## Limitations and Linux perf status

- All numbers are **representative local/environment measurements** on the stated
  Windows/MSYS2, WSL2 or Durham HPC host, not portable performance claims.
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
- **Linux `perf` evidence is collected in WSL2 (not native/cloud Linux).** See
  the dedicated section below.

### Linux perf — collected in WSL2

Hardware-counter evidence has been collected in a **WSL2** Linux environment
(Ubuntu 24.04.4, kernel `6.6.114.1-microsoft-standard-WSL2`, GCC 13.3.0 Release)
after the BIOS/UEFI firmware-virtualization blocker was enabled. The project
builds and `ctest` passes on Linux, and the virtualized PMU exposes hardware
counters. Measured, in WSL2, on the i7-7700HQ laptop:

- **`perf stat -d`** on the steady-state Light replay path: IPC ≈ 0.73–0.74, very
  high cache-miss-of-references (66–69%), low branch-miss (< 1.4%) — the
  correctness-first node-based book is **memory-latency-bound** (pointer-chasing).
- **`perf record`** hotspots: the order-id hashtable insert/rehash and
  `OrderBook::cancel_order` (and, under single-core pinning, `__sched_yield` from
  the SPSC threads).
- **1M std-vs-pooled hot path** (`high_cancellation_1m`): the opt-in pooled book
  reaches **0 measured allocations / 0 bytes** vs 7,340,580 for the standard
  book, with guard-checksum parity — the Windows pooled result reproduced on
  Linux.
- **1M SPSC steady-state Light** across six corpora: lossless, **0 dropped**, full
  checksum parity on all six.
- **LinearModel inference replay-loop** (balanced_10k, 500k events): **+≈100 ns
  p50 and 0 added allocations** vs the inference-free hot path.

Boundaries preserved: **WSL2, one laptop, virtualized PMU** (no `LLC` events,
multiplexed counters), uncontrolled turbo — representative WSL2 measurements, not
native/cloud Linux, not portable, not production-HFT. The optional ONNX backend
was **not** built on Linux, so the ONNX replay-loop row is recorded as skipped,
not measured. `Full`-validation per-run replay is O(book/event) and does not
scale to 1M here, so the 1M evidence uses the `Light` throughput-evaluation mode
(correctness is covered by `ctest` Full validation + end-of-replay checksum
parity). See
[linux_performance_evaluation_2026_06_01.md](linux_performance_evaluation_2026_06_01.md)
and [perf_profile.md](perf_profile.md).

### Durham HPC perf — collected on Hamilton8

Hardware-counter evidence has also been collected on a **Durham Hamilton8 HPC**
Slurm compute node (Rocky Linux 8.10, kernel
`4.18.0-553.123.1.el8_10.x86_64`, AMD EPYC 7702, GCC 13.2 Release). Measured
on 2026-06-04:

- **GCC Release `ctest` passed** on the HPC checkout.
- **1M high-cancellation hot path**: the opt-in pooled book reached **0 measured
  allocations / 0 bytes** vs 7,340,580 allocations for the standard book, with
  guard-checksum parity.
- **1M steady-state SPSC** across six corpora: lossless, **0 dropped**, full
  checksum parity on all six; queue depth reached 4096, so backpressure was real.
- **LinearModel inference replay-loop** (`balanced_10k`, 500k events): **+about
  140 ns p50 and 0 added allocations** vs the inference-free standard hot path.
- **`perf stat` counters**: explicit event runs counted cycles, instructions,
  branches, branch misses, cache refs/misses, L1 loads/misses, context switches,
  migrations and page faults. `LLC-loads`/`LLC-load-misses` were unsupported.
- **`perf record` hotspots** completed for the high-cancellation hot path and
  baseline SPSC process. The balanced-10k inference `perf report --stdio`
  conversion was OOM-killed, so that hotspot report is incomplete and not used.

Boundaries preserved: **one shared Slurm allocation**, GCC evidence only, no
root control over governor/turbo, no LLC events and no portable latency/counter
claim. See
[durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md).

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
- **Done (2026-06-01): WSL2 Linux perf pass.** `perf stat -d` counters and
  `perf record` hotspots were collected in WSL2 around the steady-state replay and
  hot-path workloads, alongside a 1M std-vs-pooled hot path, 1M SPSC steady-state
  and a LinearModel inference replay-loop comparison. See
  [linux_performance_evaluation_2026_06_01.md](linux_performance_evaluation_2026_06_01.md)
  and [perf_profile.md](perf_profile.md). Representative WSL2 measurements only.
- **Done (2026-06-04): Durham Hamilton8 HPC perf pass.** GCC Release build/test,
  large-corpus latency/allocation evidence, SPSC parity, LinearModel replay-loop
  inference cost, explicit `perf stat` counters and completed hotspot reports for
  two targets were collected on a Slurm compute node. See
  [durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md).
- A **more controlled bare-metal/cloud Linux perf pass** would still help if it
  exposes LLC events, lets governor/turbo be fixed and uses a frame-pointer build
  for flamegraph-quality call graphs; hardware-counter values are never fabricated
  when a real PMU is unavailable.
- The **technical paper** can now draw its microarchitectural section from the
  measured WSL2 and Durham HPC counters, with each environment's PMU and
  governor/turbo caveats stated; a more controlled Linux profiling pass would
  further strengthen it.
