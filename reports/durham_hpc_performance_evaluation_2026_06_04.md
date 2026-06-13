# Durham HPC Linux Compute-Node Performance-Counter and Latency-Distribution Evidence (2026-06-04)

> Durham HPC Linux compute-node performance-counter and latency-distribution
> evidence under disclosed conditions. Results come from one Hamilton8 Slurm
> compute-node allocation and are specific to that environment. No login-node
> benchmark numbers are reported.

This report adds a non-WSL Linux pass for Asterion on Durham University's
Hamilton8 HPC cluster. It complements, but does not replace, the earlier Windows
and WSL2 reports. The useful signal is the combination of deterministic checksum
parity, allocation counters, latency distributions and real Linux `perf`
counters under one disclosed Slurm allocation.

## Scope

- Asterion is not production-HFT infrastructure. These numbers are one Slurm
  allocation on one shared Hamilton8 compute node and are **not portable**.
- No live trading, authenticated exchange/broker connectivity, order placement,
  profitability, alpha, predictive quality or production model serving is shown.
- Generated corpora are deterministic stress workloads, not market data and not
  market-alpha evidence.
- The opt-in `PooledOrderBook` is benchmarked as an allocation experiment; the
  correctness-first `OrderBook` and single-thread replay remain the defaults.
- `ReplayValidationMode::Light` is used for large-corpus throughput rows. It is
  a throughput-evaluation mode, not a substitute for default full validation.
- The optional ONNX Runtime backend was not built in this HPC pass, so the ONNX
  replay-loop row is recorded as skipped, not measured.

## Environment

| field | value |
| --- | --- |
| Cluster | Durham University Hamilton8 HPC |
| Login node used for setup only | `login2.ham8.dur.ac.uk` |
| Main benchmark job | Slurm job `17356789`, partition `shared`, node `cn025.ham8.dur.ac.uk` |
| Perf gate job | Slurm job `17356723`, partition `test`, node `cn120.ham8.dur.ac.uk` |
| Allocation | 1 node, 1 task, 1 CPU, 8 GiB memory for the main benchmark job |
| CPU affinity | process pinned to logical CPU `121` inside the Slurm allocation |
| OS | Rocky Linux 8.10 |
| Kernel | `4.18.0-553.123.1.el8_10.x86_64` |
| CPU | AMD EPYC 7702 64-Core Processor |
| Topology visible to job | 128 CPUs, 2 sockets, 64 cores/socket, 1 thread/core |
| RAM visible on compute node | about 250 GiB |
| Compiler | GCC/G++ 13.2.0 |
| Build type | Release, flags `-O3 -DNDEBUG` |
| CMake / Ninja | CMake 3.30.5, Ninja 1.13.2 |
| Python | 3.12.6 module |
| `perf` | `4.18.0-553.123.1.el8_10.x86_64` |
| Git | 2.43.7 |
| Source checkout | clean clone under `/nobackup/<user>/asterion_hpc_20260604/asterion-src` |
| Commit | `216f473667aede486c95fe9607e09646932ab5a1` (`216f473`) |
| Result directory | remote `/nobackup/<user>/asterion_hpc_20260604/bench_17356789`; local copy under `build/durham_hpc_remote/bench_17356789/` |

CPU governor/turbo state was not controllable from the non-root Slurm job. The
available samples for `scaling_governor` and boost/turbo settings were empty.
This is disclosed rather than normalized away.

## Build and test result

GCC Release configured and built successfully with the Hamilton module stack
(`cmake/3.30.5`, `ninja/1.13.2`, `gcc/13.2`, `python/3.12.6`).

`ctest` passed in the GCC build:

```text
100% tests passed, 0 tests failed out of 1
Total Test time (real) = 6.06 sec
```

Clang 18.1.8 was also attempted. Configure/build succeeded, but `ctest` failed a
single allocation-tracker assertion:

```text
tests/unit/test_allocation_tracking.cpp:39
REQUIRE(snapshot.allocations >= 1) with expansion 0 >= 1
170 passed | 1 failed
```

Later Clang benchmark attempts failed to load `libc++.so.1` after the module
switch. Therefore this report uses **GCC only** as accepted measurement evidence;
the Clang attempt is documented as a limitation, not counted as evidence.

## Linux `perf` availability

A short Slurm compute-node gate ran before the benchmark pass. `perf stat -d --
true` succeeded inside a Slurm allocation with `perf_event_paranoid = 2`.

Available:

- cycles
- instructions
- branches
- branch-misses
- cache-references
- cache-misses
- L1-dcache-loads
- L1-dcache-load-misses
- task-clock, context-switches, CPU migrations, page faults

Not available:

- `LLC-loads`
- `LLC-load-misses`

Some detailed-mode grouped events were reported as `<not counted>` in `perf stat
-d` because of counter grouping/multiplexing and the NMI watchdog. The benchmark
counter tables below therefore use explicit event lists. Those explicit runs
completed with exit code 0 and counted cycles/instructions/branch/cache/L1
events.

## Methodology

- No benchmark was run on the login node.
- Source was cloned into `/nobackup`, checked out at commit `216f473...`, and
  built Release with GCC 13.2.
- Deterministic corpora were generated under the ignored build/scratch tree with
  explicit seeds and SHA-256 checksums.
- Hot-path rows were warmed before measured iterations and reset allocation
  counters before the measured loop.
- The 1M steady-state SPSC rows use `--only-steady-state-replay
  --steady-state-validation-mode light --spsc-queue-capacity 4096`.
- The single-symbol hot-path benchmark skips the generated multi-symbol-style
  corpora; they are generated and checksummed for visibility only.
- `perf stat` and `perf record` were run around whole benchmark processes, so
  counter values include the selected benchmark rows in that process, not an
  isolated function probe.
- Raw `perf.data` files are not committed. Concise `perf stat` text,
  `perf report --stdio` text where available and JSON benchmark summaries were
  copied under the ignored local build directory.

The main Slurm job finished after writing benchmark JSON, `perf stat` text and
the completed hotspot reports, but Slurm marked it `OUT_OF_MEMORY` because
`perf report --stdio` for the balanced-10k inference `perf.data` was killed.
That inference hotspot report is therefore incomplete and is not interpreted
below. The high-cancellation and baseline SPSC hotspot reports completed.

## Corpus manifest

Large corpora:

| name | mode | events | seed | symbols | SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| `baseline_100k` | balanced | 100,000 | 2001 | 1 | `ed3aaf6e917e8d39529d81249d2c0a93eb099189fd628943599c3f2e47494b8b` |
| `baseline_1m` | balanced | 1,000,000 | 2002 | 1 | `373458a4b1157f6999c235c782d20b0480fd8501bdf13fc2d1c4addc9e65ed86` |
| `high_cancellation_1m` | high-cancel | 1,000,000 | 2003 | 1 | `9f31433839a3708d5723a783228b6e109e81f0925bafaed487ba51adc6e8e325` |
| `replace_heavy_1m` | replace-heavy | 1,000,000 | 2004 | 1 | `30fc80e0b7c5808b6a118c3bc05dc8719aa72d4f01ca19cbc75b24ffdfaf2bfe` |
| `deep_book_1m` | deep-book | 1,000,000 | 2005 | 1 | `b4f167142d242f9e13291dbda0212e41f0f7e6ca6d7f0d61845d4172b38a277a` |
| `bursty_1m` | bursty | 1,000,000 | 2006 | 1 | `8c5ceb658679a9ecdb1ec9eb58121e0a3c4fb5de3a80679344e15c5c5e6e151d` |
| `wide_price_range_1m` | wide-price-range | 1,000,000 | 2007 | 1 | `0e657585c01683871aa6713a2366d4f74832071809d2164e25d92ff3f61cd818` |
| `multi_symbol_grouped_1m` | multi-symbol | 1,000,000 | 2008 | 8 | `89bb120885f205bdc071781ad0f41bc54f0bd841b8255b3ed66f6634a2e58ca5` |
| `balanced_10k` | balanced | 10,000 | 2101 | 1 | `81afa556f4de66c9089fd718a5be85ad1c0dd7b22b97f6f7f6ccedbe487f4068` |

Small stress corpora:

| name | mode | events | seed | symbols | SHA-256 |
| --- | --- | ---: | ---: | ---: | --- |
| `stress_baseline_balanced` | balanced | 4,000 | 20260531 | 1 | `13a9d81d519893ba5048aa35fad16dcaede0c222f9cee3003cc6d023139fafb1` |
| `stress_high_cancellation` | high-cancel | 4,000 | 20260532 | 1 | `b22394e8b5c4de5530d705e48c59ca831ab0c397e24f2df156473d275dfb9da7` |
| `stress_replace_heavy` | replace-heavy | 4,000 | 20260533 | 1 | `06d5c2da0ca22321bd2ee4199e1b3d236382c466655a815c7be5ce40778f2204` |
| `stress_deep_book` | deep-book | 5,000 | 20260534 | 1 | `69b309147f2202877aac169c763d708c442b26987e85c50fbd6635f197e9e6fe` |
| `stress_wide_price_range` | wide-price-range | 5,000 | 20260535 | 1 | `61725c65f230b5ac47bde7ec59111325a6f488a0e724a976c86c4ab2c799b06f` |
| `stress_bursty_flow` | bursty | 4,000 | 20260536 | 1 | `276d3e0eb39ec9d60de2566a06d13489649e7b6ec5776b8db7446ad8ad077c3c` |
| `stress_long_same_symbol` | long-same-symbol | 8,000 | 20260537 | 1 | `588434b28528517b1441caba491aee8fe900efd7e28b53835424a8bc87a0886d` |
| `stress_adversarial_lifecycle` | adversarial-lifecycle | 4,000 | 20260538 | 1 | `74da8c3ebeec5da3f486c74068a5fa81f1d3f9e1ce4e08131a95ff58e64a783b` |
| `stress_multi_symbol_style` | multi-symbol | 4,000 | 20260539 | 4 | `ad9e4a83c73c917b890d9ba028c377fb15aaac85580eab5b51831943d1977f67` |

## High-cancellation 1M hot path

Dataset: `high_cancellation_1m`, 1,000,000 events. Five measured iterations =
5,000,000 events per row.

| row | p50 | p95 | p99 | p99.9 | max | throughput | allocations | bytes | guard |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| standard `hot_path_binary_replay_l3_l2_strategy_risk` | 281 ns | 501 ns | 631 ns | 812 ns | 207,601 ns | 2,879,594 ev/s | 7,340,580 | 461,929,200 | `14180005740461440914` |
| pooled `hot_path_binary_replay_pooled_l3_l2_strategy_risk` | 291 ns | 491 ns | 681 ns | 832 ns | 213,673 ns | 2,815,909 ev/s | **0** | **0** | `14180005740461440914` |

The pooled path reaches zero measured steady-state allocations and preserves the
same guard checksum. Latency is effectively comparable in this run; the
defensible improvement is allocation removal under parity.

## Small stress standard-vs-pooled hot path

All rows preserve guard checksum parity between standard and pooled variants.
The `events` column is total measured event count after iterations.

| corpus | events | standard p50 | standard p99 | standard allocs | pooled p50 | pooled p99 | pooled allocs |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `stress_adversarial_lifecycle` | 20,000 | 200 ns | 361 ns | 30,000 | 180 ns | 311 ns | **0** |
| `stress_baseline_balanced` | 20,000 | 300 ns | 441 ns | 28,125 | 251 ns | 411 ns | **0** |
| `stress_bursty_flow` | 20,000 | 341 ns | 511 ns | 28,275 | 281 ns | 461 ns | **0** |
| `stress_deep_book` | 25,000 | 331 ns | 541 ns | 46,205 | 271 ns | 470 ns | **0** |
| `stress_high_cancellation` | 20,000 | 250 ns | 421 ns | 29,390 | 200 ns | 380 ns | **0** |
| `stress_long_same_symbol` | 40,000 | 321 ns | 471 ns | 51,895 | 270 ns | 441 ns | **0** |
| `stress_replace_heavy` | 20,000 | 321 ns | 421 ns | 34,260 | 270 ns | 381 ns | **0** |
| `stress_wide_price_range` | 25,000 | 491 ns | 772 ns | 43,125 | 421 ns | 882 ns | **0** |

`stress_multi_symbol_style` was generated and checksummed but skipped by the
single-symbol hot-path benchmark.

## 1M steady-state SPSC replay

Command shape: `--only-steady-state-replay --steady-state-validation-mode light
--spsc-queue-capacity 4096`.

| corpus | single-thread ev/s | SPSC ev/s | ratio | backpressure | dropped | max depth | checksum parity | allocations single / SPSC |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| `baseline_1m` | 1,551,396 | 1,221,553 | 0.79 | 245 | 0 | 4096 | true | 1,749,373 / 1,749,376 |
| `high_cancellation_1m` | 3,748,307 | 4,627,770 | 1.23 | 244 | 0 | 4096 | true | 1,468,119 / 1,468,122 |
| `replace_heavy_1m` | 1,690,788 | 1,368,047 | 0.81 | 245 | 0 | 4096 | true | 1,947,368 / 1,947,371 |
| `deep_book_1m` | 1,007,316 | 816,142 | 0.81 | 246 | 0 | 4096 | true | 2,547,216 / 2,547,219 |
| `bursty_1m` | 1,609,860 | 1,170,675 | 0.73 | 246 | 0 | 4096 | true | 1,747,761 / 1,747,764 |
| `wide_price_range_1m` | 1,273,384 | 896,940 | 0.70 | 246 | 0 | 4096 | true | 1,752,633 / 1,752,636 |

The SPSC path is lossless (`dropped = 0`) and checksum-identical on all six
large corpora. The bounded queue reaches capacity, so backpressure is real. The
allocation counts are the correctness-first node-book storage cost; this is not
an allocation-free SPSC result.

## Balanced 10k inference replay-loop

Dataset: `balanced_10k`, 10,000 events. Hot-path rows use 50 measured iterations
= 500,000 events.

| row | p50 | p95 | p99 | p99.9 | max | throughput | allocations | bytes | guard |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| inference-free standard hot path | 371 ns | 511 ns | 601 ns | 831 ns | 232,147 ns | 2,132,930 ev/s | 703,450 | 39,408,800 | `892861025581029264` |
| pooled hot path | 310 ns | 450 ns | 541 ns | 751 ns | 200,518 ns | 2,507,441 ev/s | **0** | **0** | `892861025581029264` |
| LinearModel replay-loop inference | 511 ns | 671 ns | 792 ns | 1,002 ns | 206,679 ns | 1,649,716 ev/s | 703,450 | 39,408,800 | `1442857765714779360` |

The LinearModel event-loop inference row adds about 140 ns p50 over the
inference-free standard hot path in this run and adds **0 measured allocations**
on top of the correctness-first node book. The guard differs because the model
score and policy result are folded into the inference guard checksum.

Standalone inference-support rows from the same run:

| row | p50 | p99 | throughput | allocations |
| --- | ---: | ---: | ---: | ---: |
| `feature_extraction_caller_owned_buffer` | 30 ns | 40 ns | 16,441,266 op/s | **0** |
| `linear_inference_only` | 30 ns | 40 ns | 16,967,916 op/s | **0** |
| `feature_buffer_measured_linear_inference` | 121 ns | 151 ns | 6,462,928 op/s | **0** |

ONNX Runtime was not compiled into this build, so
`hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk` was
skipped rather than timed through a fallback.

## `perf stat` counters

Counter rows were collected with explicit event lists. LLC events are not shown
because the Hamilton8 kernel/PMU combination reported them as unsupported for
this user allocation.

| process | task-clock | elapsed | cycles | instructions | IPC | branch-miss | cache-miss of refs | L1 miss | ctx switch | migrations | page faults |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| high-cancellation 1M hot path | 15,718.40 ms | 15.7249 s | 40.306B | 77.185B | 1.915 | 1.06% | 5.45% | 0.52% | 0 | 0 | 22,503 |
| baseline 1M steady SPSC | 13,272.42 ms | 13.2775 s | 32.959B | 33.044B | 1.003 | 0.80% | 43.71% | 3.19% | 0 | 0 | 31,419 |
| replace-heavy 1M steady SPSC | 12,186.39 ms | 12.1926 s | 29.509B | 31.877B | 1.080 | 0.81% | 43.03% | 2.95% | 0 | 0 | 25,368 |
| deep-book 1M steady SPSC | 19,410.29 ms | 19.4181 s | 49.445B | 44.951B | 0.909 | 1.20% | 35.68% | 4.52% | 0 | 0 | 56,632 |
| balanced 10k inference process | 224,643.70 ms | 224.6730 s | 557.545B | 1,125.320B | 2.018 | 1.15% | 13.69% | 7.98% | 0 | 0 | 11,793 |

The balanced-10k inference `perf stat` row is a whole-process run of the default
benchmark selection plus inference rows, so it is much longer than the isolated
hot-path JSON row. Treat it as process-level counter context, not an isolated
per-inference counter profile.

## Hotspots

`perf record` completed for all three targets, but `perf report --stdio` was
killed by OOM for the balanced-10k inference data. Only the completed reports are
interpreted here.

High-cancellation 1M hot path, top completed report rows:

| symbol / area | sample share |
| --- | ---: |
| `append_to_checksum` | 19.03% children / 18.96% self |
| `fnv1a_append_byte` | 7.09% self |
| `operator new` | 6.52% self |
| `OrderBook::check_invariants` | 5.96% |
| `clock_gettime` / vDSO timing | about 5.9% / 5.6% |
| `RiskGateway::check_new_order` | 5.14% |

The call graph also contains expected node-container and book-lifecycle work,
including hashtable insert/rehash and `OrderBook::cancel_order`. Because the
build is optimized Release (`-O3`) without a frame-pointer-specific profiling
build, call graphs are partial.

Baseline 1M steady SPSC, top completed report rows:

| symbol / area | sample share |
| --- | ---: |
| `append_to_checksum` | 14.90% |
| `OrderBook::find_order` | 12.43% |
| `fnv1a_append_byte` | 7.32% |
| `std::_Hashtable<...OrderBook::Locator...>::find` | 6.87% |
| `OrderBook::checksum` | 6.75% |
| `OrderBook::check_invariants` | 5.93% |
| `malloc_consolidate` | 5.28% |
| `unlink_chunk` | 3.60% |
| `operator new` | 2.50% |
| `OrderBook::cancel_order` | 2.39% |
| `_M_rehash` | 2.33% |

`__sched_yield` appears around 0.82% in this Hamilton SPSC profile and is not
dominant. That differs from the earlier WSL2 pinned profile where scheduler
yielding dominated due to pinning producer and consumer onto one laptop CPU.

## Interpretation

- The pooled book again removes measured steady-state allocations in the scoped
  hot-path benchmark while preserving guard checksum parity.
- The correctness-first node book remains allocation-heavy by design on add /
  replace / cancel workloads because it uses standard node-based containers and
  order-id lookup structures. Those allocations are reported, not hidden.
- The large-corpus SPSC path is lossless and checksum-identical under a bounded
  queue; throughput varies by corpus and remains a systems-evaluation result,
  not a production networking claim.
- The LinearModel replay-loop inference path adds small but measurable local
  event-loop cost and no additional allocations over the node book in this GCC
  run. It is model-plumbing evidence only.
- HPC counters expose cycles/instructions/branch/cache/L1 data but not LLC data.
  A dedicated frame-pointer profiling build would be needed for stronger
  flamegraph-quality attribution.

## Limitations

- Single Slurm allocation on a shared HPC node; no cross-node, cross-day or
  cross-cluster portability claim.
- One CPU allocated, with no root control over governor/turbo, NMI watchdog or
  PMU scheduling.
- LLC events were unsupported; some grouped events were multiplexed or not
  counted. Explicit counter runs are used where available.
- `perf record` call graphs are partial because this was the normal Release
  build, not a frame-pointer profiling build.
- The main Slurm job has state `OUT_OF_MEMORY` because one `perf report --stdio`
  conversion for the balanced-10k inference profile was killed after benchmark
  JSON and counter files had already been written. The incomplete inference
  hotspot report is not interpreted.
- Clang evidence is not accepted because the Clang test lane failed one
  allocation-tracker assertion and later benchmark attempts failed to resolve
  `libc++.so.1`.
- Raw benchmark JSON, generated corpora and raw `perf.data` remain ignored build
  artifacts, not committed fixtures.
