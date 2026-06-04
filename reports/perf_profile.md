# Perf Profile

Representative local measurements on one WSL2 Linux environment on this laptop,
plus one Durham Hamilton8 HPC Slurm compute-node allocation, not portable
performance claims. Not live trading, not production HFT, not a portable latency
proof.

## Durham Hamilton8 HPC status (updated 2026-06-04)

Linux `perf` was also run on Durham Hamilton8 under Slurm, on compute node
`cn025.ham8.dur.ac.uk` (main benchmark job `17356789`, partition `shared`, 1
task / 1 CPU / 8 GiB, CPU affinity 121). This was a compute-node run, not a
login-node benchmark.

Environment:

- Rocky Linux 8.10, kernel `4.18.0-553.123.1.el8_10.x86_64`.
- AMD EPYC 7702 64-Core Processor, topology visible to job: 128 CPUs, 2 sockets,
  64 cores/socket, 1 thread/core.
- GCC/G++ 13.2.0, Release `-O3 -DNDEBUG`, CMake 3.30.5, Ninja 1.13.2, Python
  3.12.6, `perf 4.18.0-553.123.1.el8_10.x86_64`.
- Source commit `216f473667aede486c95fe9607e09646932ab5a1`; GCC `ctest`
  passed. Clang 18 was attempted but is not accepted evidence because one
  allocation-tracker assertion failed and later benchmark attempts could not
  resolve `libc++.so.1`.

`perf stat -d -- true` succeeded inside Slurm with `perf_event_paranoid = 2`.
Explicit event runs counted cycles/instructions/branches/branch-misses/cache
refs/cache misses/L1 loads/L1 misses/context/page-fault events. `LLC-loads` and
`LLC-load-misses` were `<not supported>`, and governor/turbo were not
controllable without root.

Representative counter rows:

| process | IPC | branch-miss % | cache-miss % of refs | L1 miss % | context switches | migrations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| high-cancellation 1M hot path | 1.915 | 1.06 | 5.45 | 0.52 | 0 | 0 |
| baseline 1M steady SPSC | 1.003 | 0.80 | 43.71 | 3.19 | 0 | 0 |
| replace-heavy 1M steady SPSC | 1.080 | 0.81 | 43.03 | 2.95 | 0 | 0 |
| deep-book 1M steady SPSC | 0.909 | 1.20 | 35.68 | 4.52 | 0 | 0 |
| balanced 10k inference process | 2.018 | 1.15 | 13.69 | 7.98 | 0 | 0 |

Completed hotspot reports:

- High-cancellation 1M hot path: `append_to_checksum` (~19%), FNV checksum byte
  append, `operator new`, `OrderBook::check_invariants`, `clock_gettime`/vDSO
  timing and `RiskGateway::check_new_order` were the largest rows.
- Baseline 1M steady SPSC: checksum work, `OrderBook::find_order`, hashtable
  lookup, `OrderBook::checksum`, invariant checks and allocator/container work
  dominated. `__sched_yield` was around 0.82%, not dominant.

The balanced-10k inference `perf record` completed, but `perf report --stdio`
was OOM-killed and the Slurm job ended in state `OUT_OF_MEMORY` after benchmark
JSON and other counter files had already been written. That inference hotspot
report is incomplete and should not be interpreted.

Full details are in
[durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md).

## Local Status (updated 2026-06-01)

Linux `perf` **was run** in a WSL2 environment on this host. The earlier
firmware-virtualization blocker is **resolved** (BIOS/UEFI virtualization is now
enabled), so WSL2 boots a Linux kernel, the project builds and `ctest` passes,
and **hardware PMU counters are exposed**. No counter value in this report is
fabricated; counters that the virtualized PMU does not expose are recorded as
`<not supported>` verbatim.

Environment (see
[linux_performance_evaluation_2026_06_01.md](linux_performance_evaluation_2026_06_01.md)
for the full table):

- **WSL2** (Microsoft Hyper-V), Ubuntu 24.04.4 LTS, kernel
  `6.6.114.1-microsoft-standard-WSL2`. This is WSL2, **not** native or cloud Linux.
- CPU Intel i7-7700HQ (Kaby Lake), 4c/8t. RAM visible to WSL2 ≈ 7.7 GiB.
- GCC 13.3.0, Release `-O3 -DNDEBUG`, CMake 3.28.3, Ninja 1.11.1, Python 3.12.3.
- `perf 6.8.12` from `linux-tools-6.8.0-124-generic`
  (`/usr/lib/linux-tools/6.8.0-124-generic/perf`). The WSL2 kernel ships no
  matching `linux-tools`, so the `/usr/bin/perf` wrapper prints
  `WARNING: perf not found for kernel 6.6.114.1-microsoft`; the generic binary
  was invoked directly and works. `perf_event_paranoid = 2`; runs were root.
- CPU governor/turbo are **not controllable** in WSL2 (no `cpufreq`, no
  `intel_pstate`); observed frequency under load ~2.4–2.9 GHz. Affinity pinning
  (`taskset -c 2`) **does** work and all `perf` runs are pinned.

## Were PMU counters available?

Yes, with one gap. `perf stat -d -- true` returns hardware counters:
`cycles, instructions, branches, branch-misses, cache-references, cache-misses,
L1-dcache-loads, L1-dcache-load-misses` all count; **`LLC-loads` /
`LLC-load-misses` report `<not supported>`** on this virtualized PMU. Because
more events are requested than there are counters, `perf` **time-multiplexes**
and reports a per-counter scaling fraction (≈49–75%); the counts are `perf`'s
scaled estimates, recorded as-is.

## Commands run

```bash
PERF=/usr/lib/linux-tools/6.8.0-124-generic/perf
BENCH=build/asterion_benchmarks
EV=task-clock,context-switches,cpu-migrations,page-faults,cycles,instructions,branches,branch-misses,cache-references,cache-misses

# Clean O(N) replay path (steady-state Light), per 1M corpus:
$PERF stat -d -e "$EV" -- taskset -c 2 $BENCH --dataset build/perf_corpora/baseline_1m.bin \
  --only-steady-state-replay --steady-state-validation-mode light --spsc-queue-capacity 4096

# Hot path (+ per-run Full-validation rows in the same process) on a small-book corpus:
$PERF stat -d -e "$EV" -- taskset -c 2 $BENCH --dataset build/perf_corpora/high_cancellation_1m.bin \
  --only-hot-path --hot-path-warmup 1 --hot-path-iterations 5 --steady-state-validation-mode light

# Hotspots:
$PERF record -F 999 -g -- taskset -c 2 $BENCH --dataset build/perf_corpora/baseline_1m.bin \
  --only-steady-state-replay --steady-state-validation-mode light --spsc-queue-capacity 4096
$PERF report --stdio
```

## Top hotspots (perf report, `-O3` so call graphs are partial)

- **Steady-state Light replay (`baseline_1m`, pinned single core):** dominated by
  `__sched_yield` / scheduler (~19% of samples) — a **pinning artifact** from
  putting the SPSC producer and consumer on one logical CPU; user-space samples
  are in `run_spsc_replay_steady_state` and `cfree` (node-book allocate/free).
- **Hot path + per-run Full replay (`high_cancellation_1m`, pinned):** the
  order-id hashtable (`std::_Hashtable<…>::_M_insert_unique` / `_M_rehash`),
  `OrderBook::cancel_order`, and `operator new`/`malloc`/`cfree`.

## Cache / branch observations

| process (pinned, perf stat -d) | GHz | IPC | branch-miss % | cache-miss % of refs | L1-dcache-miss % |
| --- | ---: | ---: | ---: | ---: | ---: |
| steady-state Light `baseline_1m` | 2.920 | 0.74 | 0.89 | 69.41 | 5.78 |
| steady-state Light `replace_heavy_1m` | 2.523 | 0.73 | 0.96 | 66.08 | 5.40 |
| steady-state Light `deep_book_1m` | 2.522 | 0.73 | 1.37 | 68.34 | 7.62 |
| hot path + Full replay `high_cancellation_1m` | 2.417 | 1.33 | 1.35 | 47.57 | 1.15 |

The correctness-first replay path is **memory-latency-bound** (IPC ≈ 0.73–0.74,
66–69% cache-miss-of-refs, < 1.4% branch-miss): the node-based book (price
levels, FIFO queues, hashed order-id index) is pointer-chasing with poor spatial
locality, worst on the deepest book (`deep_book` L1-miss 7.62%). The
hot-path-plus-validation process has higher IPC (1.33) because `check_invariants()`
does dense in-cache hashtable work — confirming validation, not the trading hot
path, dominates that process.

## Standard vs pooled (interpretation)

On Linux the opt-in `PooledOrderBook` reached **0 measured steady-state
allocations / 0 bytes** on the 1M `high_cancellation` hot path (vs 7,340,580 for
the standard book) while matching the standard book's guard checksum
`14180005740461440914` bit-for-bit. The memory-bound counters above are exactly
the cost the pooled book removes; see
[linux_performance_evaluation_2026_06_01.md](linux_performance_evaluation_2026_06_01.md).

## SPSC (interpretation)

The unpinned steady-state runs are lossless and bit-identical to single-thread on
all six 1M corpora (0 dropped, full checksum parity, bounded queue saturating).
Under `perf` pinning to a single core the two SPSC threads interleave through
`sched_yield`, so the pinned `perf record` is a cost probe, not the throughput
measurement — the unpinned throughput table in the evaluation report is the
representative SPSC result.

## Caveats for this environment

- **WSL2, not native/cloud Linux.** Virtualized PMU (no LLC events, multiplexed
  counters), uncontrolled turbo, one laptop — representative local measurements,
  not portable or production-HFT.
- **Durham HPC, one shared Slurm allocation.** The Hamilton8 run is not WSL2 and
  not a login-node benchmark, but it is still one shared allocation with no LLC
  events, no root control over governor/turbo and partial `-O3` call graphs.
- `Full`-validation per-run replay is O(book/event) and does not scale to 1M
  here; 1M evidence uses `Light` (a throughput-evaluation mode). Correctness is
  covered by `ctest` (Full validation) and end-of-replay checksum parity.
- Raw `perf.data` is not committed; only concise text reports/counters are
  recorded. Large generated corpora are git-ignored.

## Allocator profiling (optional, not in default CI)

```bash
heaptrack ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin \
  --only-steady-state-replay --steady-state-validation-mode light
valgrind --tool=massif ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin \
  --only-steady-state-replay --steady-state-validation-mode light
```

## More controlled Linux / flamegraph (future)

On a more controlled Linux host or Slurm allocation, add `LLC` cache events when
available, a fixed governor/turbo when permitted, and DWARF or frame-pointer call
graphs for flamegraph-quality profiles:

```bash
cmake -S . -B build-perf -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CXX_FLAGS="-fno-omit-frame-pointer"
perf record -F 999 -g -- ./build-perf/asterion_benchmarks <args>
perf script | stackcollapse-perf.pl | flamegraph.pl > asterion.svg
```
