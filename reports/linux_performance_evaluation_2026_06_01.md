# Linux Performance-Counter and Latency-Distribution Evidence (2026-06-01)

> **Linux performance-counter and latency-distribution evidence under disclosed
> local conditions.** These are representative measurements on one WSL2 Linux
> environment on one laptop, not portable performance claims. Nothing here is a
> production-HFT, portable-latency, live-trading, profitability/alpha,
> predictive-quality or production-model-serving claim. Every number below was
> measured in this WSL2 environment; no Windows/MSYS2 numbers are copied into the
> Linux result tables, and no `perf` counter is fabricated.

This report adds the previously-deferred Linux `perf` pass for Asterion. The
earlier firmware-virtualization blocker (BIOS/UEFI virtualization disabled, which
prevented WSL2 from booting a Linux kernel) is **resolved**: WSL2 now boots, the
project builds and tests pass on Linux, and the host **does expose hardware PMU
counters**, so this run records real cycles/instructions/IPC/branch/cache
counters around the replay and hot-path workloads, plus `perf record` hotspots.

## Scope Boundary

- Asterion is not production-HFT infrastructure. These numbers are one local WSL2
  environment on one Kaby Lake laptop and are **not portable**.
- No live trading, authenticated exchange/broker connectivity, order placement,
  profitability, alpha, predictive quality or production model serving is shown.
- The pooled `PooledOrderBook` is opt-in; the correctness-first `OrderBook` and
  single-thread replay remain the defaults.
- Generated corpora are deterministic stress workloads, not market data and not
  market-alpha evidence. The optional ONNX Runtime backend was **not** compiled
  into this Linux build, so the ONNX replay-loop row is recorded as skipped, not
  measured (see [Inference replay-loop](#inference-replay-loop-linearmodel)).
- This is **WSL2**, not native Linux or cloud Linux. WSL2 runs a Microsoft
  virtualized kernel; CPU governor/turbo are not controllable here and the PMU is
  a virtualized PMU. Counter absolutes are environment-specific.

## Environment

| field | value |
| --- | --- |
| Host class | **WSL2** (Windows Subsystem for Linux 2), Microsoft Hyper-V; not native/cloud Linux |
| OS | Ubuntu 24.04.4 LTS (Noble Numbat) under WSL2 |
| Kernel | `6.6.114.1-microsoft-standard-WSL2` (`#1 SMP PREEMPT_DYNAMIC`) |
| CPU | Intel(R) Core(TM) i7-7700HQ @ 2.80GHz (Kaby Lake), 4 cores / 8 threads |
| Virtualization | type `full`, hypervisor vendor `Microsoft` |
| RAM visible to WSL2 | 8,077,420 kB ≈ 7.7 GiB (host has 16 GiB; WSL2 caps guest memory ~50%) |
| Compiler | GCC 13.3.0 (`Ubuntu 13.3.0-6ubuntu2~24.04.1`) |
| Build type | Release, flags `-O3 -DNDEBUG` |
| CMake / Ninja | CMake 3.28.3, Ninja 1.11.1 |
| Python | 3.12.3 |
| Git | 2.43.0 |
| `perf` | `perf 6.8.12` from `linux-tools-6.8.0-124-generic` (`/usr/lib/linux-tools/6.8.0-124-generic/perf`) |
| Benchmark commit field | `9ca99f73674c` (matches repo HEAD `9ca99f7…`) |
| Build/source tree | clean `git clone` of the repo into the native ext4 filesystem (`/root/asterion`), not the `/mnt/c` drvfs mount, so build and replay I/O are native-fs |

`perf` note: the WSL2 kernel ships no matching `linux-tools`, so the `/usr/bin/perf`
wrapper prints `WARNING: perf not found for kernel 6.6.114.1-microsoft`. The
`linux-tools-6.8.0-124-generic` `perf` binary was invoked directly and works.
`perf_event_paranoid` is `2`; the benchmarks were run as root.

**Controllability of CPU state (disclosed):** WSL2 exposes **no** `cpufreq`
governor (`/sys/.../scaling_governor` absent) and **no** `intel_pstate`
(`no_turbo` absent), so frequency governor and turbo were **not controllable or
pinnable** here. CPU **affinity pinning works** (`taskset -c 2`) and all `perf`
runs below are pinned to logical CPU 2. Observed `perf`-reported core frequency
under load was ~2.4–2.9 GHz (see the perf-stat table), i.e. turbo was active and
uncontrolled. This frequency variability is part of the disclosed local
conditions.

## Linux `perf` availability

`perf stat -d -- true` **succeeds and returns hardware counters** on this host:

```text
$ perf stat -d -- true     # (linux-tools-6.8.0-124-generic/perf)
   cycles, instructions, branches, branch-misses,
   cache-references, cache-misses, L1-dcache-loads, L1-dcache-load-misses  -> counted
   LLC-loads, LLC-load-misses                                              -> <not supported>
```

So unlike the prior firmware-blocked attempts, hardware PMU counters are
available here, **except** the detailed-mode last-level-cache events
(`LLC-loads` / `LLC-load-misses`), which the virtualized PMU reports as
`<not supported>`. Because more events are requested than there are hardware
counters, `perf` **time-multiplexes** the counters; the per-counter scaling
fraction (≈49–75% in the tables below) is reported verbatim and the counts are
`perf`'s scaled estimates. These are honest counter estimates under
multiplexing, not exact event totals.

## Methodology

- **Native-fs build.** A clean `git clone` of HEAD was built in the ext4 root
  filesystem (not `/mnt/c`) with `cmake -S . -B build -G Ninja
  -DCMAKE_BUILD_TYPE=Release`, then `cmake --build build`, then
  `ctest --test-dir build --output-on-failure` (**1/1 passed, 8.67 s**).
- **Deterministic corpora.** Corpora were generated by
  `scripts/generate_synthetic_events.py` with the same modes/seeds as the existing
  performance docs. The generated 100k/1M binaries are **byte-identical** to the
  documented manifest (SHA-256 match on all 8 corpora — see
  [Corpora](#corpora)), so the *shape* of the workload reproduces across
  platforms even though the timings do not.
- **Validation modes and a runtime caveat that shaped this run.** Replay defaults
  to correctness-first `Full` validation (a full book-invariant walk after every
  event). `OrderBook::check_invariants()` is **O(resting-book size) per event**
  and allocates a scratch set per call, so for book-growing corpora the per-run
  `Full`-validation replay rows are effectively O(N·book) and do not scale to 1M
  events in this environment (a single balanced-100k `--only-hot-path` pass did
  not finish in 15 min). This is exactly why the repo documents
  `ReplayValidationMode::Light` as the **large-corpus throughput path**. The 1M
  evidence below therefore uses `--only-steady-state-replay --steady-state-validation-mode light`
  (cheap per-event top-of-book checks, full walk deferred to end-of-replay).
  **Light is a throughput-evaluation mode, not a replacement for correctness
  testing**; correctness is covered by `ctest` (Full validation) and by the
  end-of-replay checksum parity reported here.
- **Warm-up vs measured.** Hot-path and inference rows reset latency/allocation
  counters after warm-up, so reported counts are steady-state. The pooled book
  is warmed before the measured loop so its zero-allocation result is a
  warmed-path result.
- **Checksum parity is structural.** The SPSC and pooled paths are validated by
  matching the single-thread/standard book, execution-report, diagnostics and
  benchmark guard checksums bit-for-bit, not by coincidence.
- **`perf` runs are pinned** with `taskset -c 2`. `perf` counters cover the whole
  benchmark process; the chosen `--only-…` mode selects which rows run inside
  that process (noted per row). For the steady-state perf runs the process is the
  clean O(N) replay path; for the hot-path perf run the process also includes the
  per-run `Full`-validation replay rows (noted below).
- **Local-machine dependence.** Every latency/throughput/counter value depends on
  this laptop, WSL2 scheduling, uncontrolled turbo, the virtualized PMU and timer
  behaviour. Distributions, not best cases, are reported.

## Build flags

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release   # GCC 13.3.0, -O3 -DNDEBUG
cmake --build build
ctest --test-dir build --output-on-failure                # 1/1 passed (8.67 s)
```

ONNX Runtime was intentionally **not** enabled (`-DASTERION_USE_ONNXRUNTIME` left
OFF), keeping the default dependency-light build; the ONNX replay-loop row is
consequently recorded as skipped.

## Corpora

Generated under the git-ignored `build/perf_corpora/`. SHA-256 values **match the
documented manifest** from the existing evaluation, confirming deterministic
cross-platform generation.

| corpus | mode | seed | events | symbols | size bytes | SHA-256 |
| --- | --- | ---: | ---: | ---: | ---: | --- |
| `baseline_100k` | balanced | 2001 | 100,000 | 1 | 5,800,016 | `ed3aaf6e…494b8b` |
| `baseline_1m` | balanced | 2002 | 1,000,000 | 1 | 58,000,016 | `373458a4…65ed86` |
| `high_cancellation_1m` | high-cancel | 2003 | 1,000,000 | 1 | 58,000,016 | `9f314338…e8e325` |
| `replace_heavy_1m` | replace-heavy | 2004 | 1,000,000 | 1 | 58,000,016 | `30fc80e0…af2bfe` |
| `deep_book_1m` | deep-book | 2005 | 1,000,000 | 1 | 58,000,016 | `b4f16714…b38a277a` |
| `bursty_1m` | bursty | 2006 | 1,000,000 | 1 | 58,000,016 | `8c5ceb65…e6e151d` |
| `wide_price_range_1m` | wide-price-range | 2007 | 1,000,000 | 1 | 58,000,016 | `0e657585…f3f61cd818` |
| `multi_symbol_grouped_1m` | multi-symbol | 2008 | 1,000,000 | 8 | 58,000,016 | `89bb1208…e58ca5` (generated only; single-symbol hot path is not a multi-symbol router) |
| `balanced_10k` | balanced | 2101 | 10,000 | 1 | 580,016 | `81afa556…f4068` (small-book corpus for the feasible inference-loop pass) |

Example generation command (recorded in `build/perf_corpora` manifest):

```bash
python3 scripts/generate_synthetic_events.py --mode balanced --events 1000000 \
  --seed 2002 --symbol 1 --symbols 1 --output build/perf_corpora/baseline_1m.bin --format binary
```

## Benchmark commands

```bash
PERF=/usr/lib/linux-tools/6.8.0-124-generic/perf
BENCH=build/asterion_benchmarks

# 1M steady-state Light (single-thread vs SPSC), one per corpus:
$BENCH --dataset build/perf_corpora/<corpus>.bin --only-steady-state-replay \
       --steady-state-validation-mode light --spsc-queue-capacity 4096 --json <out>.json --no-text

# 1M standard-vs-pooled hot path on the small-book high-cancellation corpus:
$BENCH --dataset build/perf_corpora/high_cancellation_1m.bin --only-hot-path \
       --hot-path-warmup 1 --hot-path-iterations 5 --steady-state-validation-mode light --json <out>.json --no-text

# Full default pass (LinearModel replay-loop inference row + micro-inference) on a small balanced corpus:
$BENCH --dataset build/perf_corpora/balanced_10k.bin --hot-path-warmup 5 --hot-path-iterations 50 \
       --steady-state-validation-mode light --json <out>.json --no-text

# perf stat -d, pinned, around the steady-state and hot-path processes:
$PERF stat -d -e task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,\
branches,branch-misses,cache-references,cache-misses -- taskset -c 2 $BENCH <args…>

# perf record hotspots, pinned:
$PERF record -F 999 -g -- taskset -c 2 $BENCH <args…> ; $PERF report --stdio
```

## SPSC steady-state replay — 1M corpora (single-thread vs SPSC)

`--only-steady-state-replay --steady-state-validation-mode light`,
`--spsc-queue-capacity 4096`. These are clean O(N) replay-path runs (no per-run
`Full`-validation rows), unpinned, not under `perf`. `timing_mode = aggregate`,
so the meaningful figure is throughput, not per-event percentiles. The
single-thread and SPSC rows share book / execution-report / diagnostics / guard
checksums **bit-for-bit** (parity column).

| corpus | single-thread ev/s | SPSC ev/s | SPSC/single | backpressure | dropped | max queue depth | parity |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | :---: |
| `baseline_1m` | 1,225,951 | 1,166,555 | 0.95 | 351,009 | 0 | 4,096 | true |
| `bursty_1m` | 1,152,931 | 1,096,812 | 0.95 | 366,419 | 0 | 4,096 | true |
| `wide_price_range_1m` | 855,712 | 976,490 | 1.14 | 466,524 | 0 | 4,096 | true |
| `deep_book_1m` | 771,702 | 834,378 | 1.08 | 281,606 | 0 | 4,096 | true |
| `replace_heavy_1m` | 1,123,340 | 559,311 | 0.50 | 435,453 | 0 | 4,096 | true |
| `high_cancellation_1m` | 3,371,318 | 1,242,996 | 0.37 | 166,761 | 0 | 4,096 | true |

Observations (local):
- **Lossless and bit-identical on 6/6 corpora**: 0 dropped events, full
  checksum/guard parity, and the bounded queue genuinely saturates
  (`max_queue_depth = capacity = 4096`, hundreds of thousands of backpressure
  waits).
- The SPSC/single ratio is workload-dependent. When per-event work is large
  (`deep_book`, `wide_price_range`) the SPSC consumer keeps up or slightly leads;
  when per-event work is tiny (`high_cancellation` single-thread hits 3.37M ev/s)
  the queue/sync overhead dominates and the ratio drops to 0.37. This is a
  systems-overhead observation, not a latency guarantee.
- The steady-state replay path is **not allocation-free**: it uses the
  correctness-first node-based `OrderBook`, so it allocates 1.47M–2.55M times
  (92–158 MB) per 1M-event run. Those allocations are reported, not hidden; the
  zero-allocation result is the **opt-in pooled book** below.

## Standard vs pooled hot path — 1M (high_cancellation_1m)

`--only-hot-path --hot-path-warmup 1 --hot-path-iterations 5`
(5 measured iterations × 1,000,000 events = 5,000,000 measured events),
unpinned clean JSON. `high_cancellation_1m` is used for the 1M hot-path point
because its book stays small, keeping the bundled per-run `Full`-validation rows
tractable. Both rows produced **identical guard checksum
`14180005740461440914`** (parity).

| path | p50 ns | p95 ns | p99 ns | p99.9 ns | max ns | throughput ev/s | allocations | bytes | parity |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: |
| standard `OrderBook` | 300 | 600 | 800 | 12,900 | 212,400 | 2,663,192 | 7,340,580 | 461,929,200 | guard match |
| pooled `PooledOrderBook` | 300 | 600 | 800 | 13,500 | 161,800 | 2,709,485 | **0** | **0** | guard match |

The opt-in pooled book reaches **0 measured steady-state allocations / 0 bytes**
on this 1M Linux run while matching the correctness-first book bit-for-bit, the
same result the Windows/MSYS2 evidence reported, now reproduced under Linux.

A smaller balanced corpus shows the same pattern (from the inference full pass,
`balanced_10k`, 50 iters = 500k events; shared guard `892861025581029264`):

| path | p50 ns | p95 ns | p99 ns | throughput ev/s | allocations | bytes |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| standard `OrderBook` | 400 | 900 | 1,700 | 1,605,219 | 703,450 | 39,408,800 |
| pooled `PooledOrderBook` | 400 | 800 | 1,400 | 1,827,539 | **0** | **0** |

## Inference replay-loop (LinearModel)

Full default pass on `balanced_10k` (50 iters = 500,000 measured events). The
event-loop inference row inserts caller-owned feature extraction → `LinearModel`
score → measured timeout/late-signal policy gate into the replay loop, alongside
the existing strategy + risk path. Compared against the inference-free hot path
on the **same corpus and iteration count**:

| row | p50 ns | p95 ns | p99 ns | p99.9 ns | throughput ev/s | allocations | guard |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| inference-free hot path (`…l3_l2_strategy_risk`) | 400 | 900 | 1,700 | 20,600 | 1,605,219 | 703,450 | `892861025581029264` |
| inference replay-loop (`…l3_l2_inference_strategy_risk`) | 500 | 1,200 | 1,800 | 19,599 | 1,267,264 | 703,450 | `1442857765714779360` |

The synchronous inference stage adds **≈ +100 ns p50** and **0 added steady-state
allocations** (703,450 = the inference-free row's count; the caller-owned
inference stage allocates nothing on top of the node-based book). The guard
differs because the inference outputs are folded into the checksum so the stage
is not optimised away; the model never alters matching, strategy or risk —
**plumbing only, no decisioning/alpha/profitability claim**.

Supporting micro-inference rows (per-call, sub-microsecond p50 dominated by the
~100 ns `steady_clock` granularity, so throughput is the better cost estimate):

| row | throughput op/s | allocations |
| --- | ---: | ---: |
| `feature_extraction_vector_returning` | 12,110,439 | 200,000 |
| `feature_extraction_caller_owned_buffer` | 17,869,610 | **0** |
| `linear_inference_only` | 18,916,669 | **0** |
| `measured_linear_inference_only` | 7,039,090 | **0** |
| `inference_policy_overhead` | 20,202,863 | **0** |
| `feature_buffer_policy_gate_overhead` | 16,219,043 | **0** |

The caller-owned feature buffer removes the one-vector-per-call allocation
(200,000 → 0) on Linux, matching the Windows result.

### Optional ONNX replay-loop — not measured on Linux

This build did **not** compile ONNX Runtime, so the optional row
`hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk` is
emitted by the benchmark under `skipped_benchmarks` with reason *"onnx runtime
not compiled in; configure with `-DASTERION_USE_ONNXRUNTIME=ON` and a
discoverable ONNX Runtime to measure."* No Linux ONNX latency/allocation numbers
are reported here; ONNX Runtime remains optional and absent from the default
build. The Windows/MSYS2 ONNX evidence is unchanged and lives in its own reports.

## Linux `perf stat -d` counters

Pinned `taskset -c 2`, counters cover the whole process. Counter multiplexing
fractions are shown in parentheses; `LLC-loads`/`LLC-load-misses` were
`<not supported>` on this virtualized PMU. The three steady-state runs are the
**clean O(N) replay path** (single-thread + SPSC steady rows only). The
high-cancellation hot-path run additionally contains the per-run
`Full`-validation replay rows, so its higher IPC reflects that
`check_invariants()` hashtable/scan work, not the hot path alone — it is included
for completeness and labelled accordingly.

| process (pinned) | task-clock ms | ctx-sw | migr | page-faults | cycles | GHz | instructions | IPC | branch-miss % | cache-miss % of refs | L1-dcache-miss % | elapsed s |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| steady-state Light `baseline_1m` | 18,071 | 3,160 | 1 | 86,654 | 52.77 B | 2.920 | 39.22 B | 0.74 | 0.89 | 69.41 | 5.78 | 18.08 |
| steady-state Light `replace_heavy_1m` | 21,096 | 3,228 | 1 | 81,683 | 53.22 B | 2.523 | 38.68 B | 0.73 | 0.96 | 66.08 | 5.40 | 21.13 |
| steady-state Light `deep_book_1m` | 28,204 | 3,538 | 0 | 119,951 | 71.14 B | 2.522 | 51.84 B | 0.73 | 1.37 | 68.34 | 7.62 | 28.24 |
| hot path + per-run Full replay `high_cancellation_1m` | 27,432 | 15,737 | 1 | 390,342 | 66.31 B | 2.417 | 88.28 B | 1.33 | 1.35 | 47.57 | 1.15 | 27.79 |

Microarchitectural reading (local):
- The steady-state replay path is **memory-latency-bound**: IPC ≈ 0.73–0.74 with a
  very high cache-miss rate (66–69% of cache references) and a low branch-miss
  rate (< 1.4%). This is the expected signature of the correctness-first
  **node-based** book — price levels, FIFO queues and a hashed order-id index are
  pointer-chasing structures with poor spatial locality. `deep_book_1m` (the
  deepest resting book, 158 MB allocated/run) shows the worst L1-dcache miss rate
  (7.62%), consistent with more pointer chasing.
- The hot-path-plus-validation process has much higher IPC (1.33) and lower
  cache-miss-of-refs (47.6%) because `check_invariants()` does dense in-cache
  hashtable work each event; this confirms it is the validation, not the trading
  hot path, that dominates that process (see hotspots below).
- Context switches and page faults are low and migrations are near zero under
  pinning; the elapsed/task-clock ratio is ≈ 1.0 (CPU-bound, single core).

## Linux `perf record` hotspots

`perf record -F 999 -g`, pinned, `perf report --stdio`. **Caveat:** the Release
build is `-O3` (frame pointers omitted), so call-graph attribution is partial and
some frames resolve as `[unknown]`; the flat per-symbol hotspots are the reliable
signal.

**Steady-state Light replay, `baseline_1m` (pinned single core):**
- `__sched_yield` and the surrounding scheduler path dominate (~19% of samples).
  This is a **pinning artifact**: pinning the SPSC producer **and** consumer to
  one logical CPU (`taskset -c 2`) forces them to interleave via `sched_yield`.
  The remaining user-space samples are inside
  `run_spsc_replay_steady_state` and `cfree` (the node-based book's
  allocate/free traffic). The unpinned throughput table above is the
  representative SPSC throughput; this pinned record is a microarchitectural/cost
  probe only.

**Hot path + per-run Full replay, `high_cancellation_1m` (pinned):**
- After the same `__sched_yield` pinning band (~15%, from the SPSC rows in the
  same process), the user-space hotspots are the **order-id hashtable**
  (`std::_Hashtable<…>::_M_insert_unique` / `_M_rehash`) and
  `OrderBook::cancel_order`, plus `operator new`/`malloc`/`cfree`. This is the
  node-based book's per-order index churn under high cancellation — the same
  structures the cache-miss counters point at.

The combined picture: Asterion's deterministic replay/book path on this host is
**allocation- and memory-latency-bound in the correctness-first book**, which is
exactly what the opt-in pooled book targets (0 allocations, see the hot-path
table) and what a future cache-friendly book layout would address.

## Limitations

- **WSL2, one laptop, not portable.** All numbers are representative local
  measurements on one WSL2 Ubuntu environment on one Kaby Lake laptop. They are
  not native Linux, not cloud Linux, not portable, and not production-HFT.
- **Virtualized PMU.** Counters come from a Microsoft virtualized PMU; LLC events
  are unavailable and other events are time-multiplexed (scaled). Absolute
  counts are environment-specific estimates.
- **Uncontrolled frequency.** WSL2 exposes no governor/turbo control here;
  observed core frequency ranged ~2.4–2.9 GHz under load. Pinning (`taskset`)
  works; frequency scaling does not.
- **Full-validation replay does not scale to 1M here.** Per-run `Full`-validation
  replay is O(book/event); the 1M evidence uses `Light` (a throughput-evaluation
  mode), and correctness is covered separately by `ctest` (Full validation) and
  the end-of-replay checksum parity reported above. `Light` is **not** a
  correctness substitute.
- **`perf` counters cover the whole process.** The hot-path perf run also
  includes per-run `Full`-validation rows; that row's counters are labelled
  accordingly and the clean replay-path counters are the steady-state rows.
- **`perf record` under pinning is yield-dominated for SPSC** and has partial
  call graphs at `-O3`; it is a cost probe, not a flamegraph-quality profile.
- **Pooled book and SPSC are opt-in.** The correctness-first `OrderBook` and
  single-thread replay remain the defaults.
- **Synthetic corpora** are deterministic stress workloads, not market data and
  not market-alpha evidence. The optional ONNX backend was not built on Linux.

## Future work

- A **native Linux or cloud Linux** run (non-virtualized PMU) would add `LLC`
  cache events, stable frequency control (governor=performance, turbo pinned) and
  flamegraph-quality call graphs (build with `-fno-omit-frame-pointer` or use
  DWARF call graphs).
- An **optional ONNX Runtime Linux build** (`-DASTERION_USE_ONNXRUNTIME=ON`) would
  let the ONNX replay-loop row be measured on Linux rather than skipped.
- A **cache-friendlier book layout** (or wider use of the pooled book) is the
  obvious target suggested by the memory-bound counters and the node-index
  hotspots.

> Linux performance-counter and latency-distribution evidence under disclosed
> local conditions — representative WSL2 measurements, not portable performance
> claims.
