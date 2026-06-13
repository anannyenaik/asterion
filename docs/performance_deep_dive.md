# Performance Deep Dive

> Measured performance-engineering case study under disclosed conditions. Every
> number is transcribed from an existing curated report in
> [`reports/`](../reports); none is recomputed or extrapolated. Results are
> specific to the stated hosts and workloads, and cross-machine comparison is
> not meaningful.

This document turns Asterion's existing benchmark evidence into a single
optimisation narrative: the baseline node-based order book, the hotspots the
profilers actually found, the opt-in `PooledOrderBook` change, the before/after
allocation and latency/throughput evidence, the correctness controls, remaining
bottlenecks and project scope.

It is a reading guide over the primary sources, not a replacement for them:

- **Primary performance context** — Durham Hamilton8 HPC Slurm compute node:
  [reports/durham_hpc_performance_evaluation_2026_06_04.md](../reports/durham_hpc_performance_evaluation_2026_06_04.md)
- Cross-report index:
  [reports/performance_evidence_summary_2026_06_01.md](../reports/performance_evidence_summary_2026_06_01.md)
- Allocation before/after:
  [reports/allocation_optimisation_report_2026_05_31.md](../reports/allocation_optimisation_report_2026_05_31.md)
- Pooled stress corpora:
  [reports/pooled_order_book_stress_report_2026_05_31.md](../reports/pooled_order_book_stress_report_2026_05_31.md)
- WSL2 perf counters/hotspots:
  [reports/linux_performance_evaluation_2026_06_01.md](../reports/linux_performance_evaluation_2026_06_01.md),
  [reports/perf_profile.md](../reports/perf_profile.md)

## 1. Summary

- The default hot path (`binary replay → L3 book update → reusable L2 view →
  fixed-size strategy callback → risk check`) uses a **correctness-first,
  node-based `OrderBook`**. Profiling shows its costs concentrate in (a) standard
  node/allocator traffic and pointer-chasing through node-based containers, and
  (b) the deterministic checksum/invariant guardrails the benchmark deliberately
  keeps in the loop.
- An **opt-in `PooledOrderBook`** replaces the node-based storage with
  vector-backed levels/orders and a reusable flat order-id index. After explicit
  warm-up it reaches **zero measured steady-state allocations** while producing
  the **same guard/replay checksums bit-for-bit** as the default book.
- The allocation result is the **defensible signal** and reproduces across three
  environments (Durham HPC, Windows/MSYS2 laptop, WSL2 laptop). Latency/throughput
  deltas are local observations: from a clear win on the small Windows fixture
  (p50 800 → 600 ns) to effectively a tie on the Durham 1M high-cancellation row
  (p50 281 → 291 ns), where the retained checksum/validation work dominates.
- The default node-based book remains available and is still the default. Nothing
  here is portable or production-grade; the limitations are listed in §9.

## 2. Measurement context

Three disclosed environments contribute evidence. They are **never compared
across machines**; each table below carries an explicit environment column.

| Environment | Label used below | Host / OS | CPU | Compiler / build | PMU notes |
| --- | --- | --- | --- | --- | --- |
| Durham Hamilton8 HPC (primary) | **Durham HPC** | one shared Slurm compute node `cn025.ham8.dur.ac.uk`, Rocky Linux 8.10 | AMD EPYC 7702 (1 CPU allocated, affinity 121) | GCC 13.2, Release `-O3 -DNDEBUG` | `perf` works; **no LLC events**; no root governor/turbo control |
| Windows/MSYS2 laptop (historical) | **Win/MSYS2** | Windows 10, MSYS2/MinGW-w64 UCRT64 | Intel i7-7700HQ | GCC 16.1.0, Release `-O3 -DNDEBUG` | not native Linux; `steady_clock` ~100 ns granularity |
| WSL2 laptop (historical) | **WSL2** | Ubuntu 24.04.4 under WSL2 | Intel i7-7700HQ | GCC 13.3.0, Release `-O3 -DNDEBUG` | virtualized PMU; no LLC events; multiplexed counters; uncontrolled turbo |

Method disciplines shared by every row (full detail in the source reports):

- **Warm-up vs measured.** Replay/hot-path rows run fixed warm-up iterations,
  then reset latency and allocation counters, so reported counts are
  steady-state. `PooledOrderBook` reservation happens *before* the measured loop,
  so its zero-allocation result is a warmed-path result.
- **Validation modes.** Replay defaults to correctness-first `Full` validation (a
  full invariant walk after every event). Per-run `Full` validation is
  O(book/event) and does not scale to 1M events, so large-corpus throughput rows
  use the opt-in `ReplayValidationMode::Light` (cheap per-event top-of-book
  checks, full walk deferred to end-of-replay). `Light` is a throughput-evaluation
  mode, **not** a correctness substitute; correctness stays covered by `ctest`
  (`Full`) and end-of-replay checksum parity.
- **Allocation counters.** An in-process allocation tracker reports counts/bytes
  after warm-up. Zeros are reported only where proven in a scoped, warmed path;
  node-book allocations are reported, never hidden.

## 3. Baseline path — the correctness-first node-based `OrderBook`

The default L3 book ([cpp/src/book/order_book.cpp](../cpp/src/book/order_book.cpp))
is built for clarity and correctness first. As the allocation report records, even
with the L2 view, strategy callback and risk accepted-ID path warmed and reserved,
Add/Replace replay still allocates **standard-library nodes**: price-level map
nodes, FIFO list nodes and node-based order-index entries. Those allocations are
deterministic and visible in the allocation counter — they are the headline cost
this case study attacks.

Two regimes appear in the measured baseline, depending on how deep the resting
book grows:

- **Small resting book** (e.g. high-cancellation, where adds and cancels churn
  but depth stays low): the per-event allocator traffic and the retained
  checksum/invariant guardrails dominate. On Durham HPC the high-cancellation 1M
  hot-path process runs at IPC **1.915** with only **5.45%** cache-miss-of-refs.
- **Deeper resting book** (baseline / replace-heavy / deep-book steady-state):
  the node-based containers become **memory-latency-bound** pointer chasing. On
  WSL2 the steady-state replay path runs at IPC **0.73–0.74** with **66–69%**
  cache-miss-of-references and **<1.4%** branch-miss — the classic signature of
  price-level maps, FIFO lists and a hashed order-id index with poor spatial
  locality (worst on the deepest book: `deep_book_1m` L1-dcache miss **7.62%**).

| Environment | Process | IPC | branch-miss % | cache-miss % of refs | L1-dcache miss % |
| --- | --- | ---: | ---: | ---: | ---: |
| Durham HPC | high-cancellation 1M hot path | 1.915 | 1.06 | 5.45 | 0.52 |
| Durham HPC | baseline 1M steady SPSC | 1.003 | 0.80 | 43.71 | 3.19 |
| Durham HPC | deep-book 1M steady SPSC | 0.909 | 1.20 | 35.68 | 4.52 |
| WSL2 | steady-state Light `baseline_1m` | 0.74 | 0.89 | 69.41 | 5.78 |
| WSL2 | steady-state Light `deep_book_1m` | 0.73 | 1.37 | 68.34 | 7.62 |

(LLC events were unsupported in both environments; WSL2 counters are multiplexed
estimates. See the source reports for the full counter tables and environment limits.)

## 4. Hotspot evidence

`perf record` hotspots from the completed Durham HPC reports (Release `-O3`, so
call graphs are partial; the flat per-symbol shares are the reliable signal):

**Durham HPC — high-cancellation 1M hot path** (small book):

| symbol / area | sample share |
| --- | ---: |
| `append_to_checksum` | 19.03% children / 18.96% self |
| `fnv1a_append_byte` | 7.09% self |
| `operator new` | 6.52% self |
| `OrderBook::check_invariants` | 5.96% |
| `clock_gettime` / vDSO timing | ≈ 5.9% / 5.6% |
| `RiskGateway::check_new_order` | 5.14% |

**Durham HPC — baseline 1M steady SPSC** (deeper book):

| symbol / area | sample share |
| --- | ---: |
| `append_to_checksum` | 14.90% |
| `OrderBook::find_order` | 12.43% |
| `fnv1a_append_byte` | 7.32% |
| `std::_Hashtable<…Locator…>::find` | 6.87% |
| `OrderBook::checksum` | 6.75% |
| `OrderBook::check_invariants` | 5.93% |
| `malloc_consolidate` | 5.28% |
| `operator new` | 2.50% |
| `OrderBook::cancel_order` | 2.39% |
| `_M_rehash` | 2.33% |

The WSL2 `perf record` pass agrees on the book-side cost: after a `__sched_yield`
band that is a single-core pinning artifact, the user-space hotspots are the
**order-id hashtable insert/rehash**, `OrderBook::cancel_order` and
`operator new`/`malloc`/`cfree` — the node-based book's per-order index churn.

**Interpreting the hotspots.** Two distinct cost families show up:

1. **Book storage / allocator / locality** — `operator new`, `malloc_consolidate`,
   `_M_rehash`, hashtable `find`, `find_order`, `cancel_order`. This is exactly
   what the pooled book targets.
2. **Deterministic guardrails** — `append_to_checksum`, `fnv1a_append_byte`,
   `OrderBook::checksum`, `OrderBook::check_invariants`, plus `clock_gettime`
   timing overhead. These are the FNV checksum and invariant walks the benchmark
   *deliberately* keeps in the loop so the optimisation cannot be "validated away".
   The pooled book does **not** remove these, which is why some latency rows tie
   even when allocations collapse to zero (§6).

## 5. The `PooledOrderBook` optimisation

`PooledOrderBook` ([cpp/include/asterion/book/pooled_order_book.hpp](../cpp/include/asterion/book/pooled_order_book.hpp),
[cpp/src/book/pooled_order_book.cpp](../cpp/src/book/pooled_order_book.cpp)) is a
**separate, opt-in** L3 implementation for the allocation-sensitive benchmark
path. The default `OrderBook` is **not rewritten**. It uses:

- vector-backed bid/ask price levels, sorted best-first;
- vector-backed order nodes linked into FIFO queues by index (not pointers);
- a reusable open-addressed (flat) order-id index;
- explicit `reserve_order_capacity(...)` warm-up before the measured loop;
- the same public book operations replay and L2 projection already use.

The intent is narrow and stated as such: **remove per-event node allocations and
improve storage locality** for a warmed replay loop, so the warmed loop can clear
and rebuild the book without paying `std::map`/`std::list` node-allocation costs
each pass — *while preserving checksum/report parity with the default book*. It is
not a production allocator, not a live-trading component and not a latency
portability claim.

The benchmark emits both rows in one pass:
`hot_path_binary_replay_l3_l2_strategy_risk` (standard) and
`hot_path_binary_replay_pooled_l3_l2_strategy_risk` (pooled).

## 6. Before / after results

### 6.1 Allocations

Standard node-based book → opt-in pooled book, after warm-up. Every row preserves
guard-checksum parity (§7).

| Environment | Workload (measured events) | Standard allocations | Pooled allocations |
| --- | --- | ---: | ---: |
| **Durham HPC** | `high_cancellation_1m`, 1M × 5 = 5,000,000 ev | 7,340,580 (461,929,200 bytes) | **0 (0 bytes)** |
| **Durham HPC** | 8 small stress corpora, 20k–40k ev each | 28,125 – 51,895 | **0** (8/8) |
| **Win/MSYS2** | `sample_hot_path_replay.bin`, 5,000 iters | 105,000 (6,720,000 bytes) | **0 (0 bytes)** |
| **Win/MSYS2** | 8 generated stress corpora, 4k–8k ev each | 140,625 – 259,475 | **0** (8/8) |
| **Win/MSYS2** | 7 larger 100k/1M corpora | 4.2M – 54M | **0** (7/7) |
| **WSL2** | `high_cancellation_1m`, 1M × 5 = 5,000,000 ev | 7,340,580 (461,929,200 bytes) | **0 (0 bytes)** |

Across every disclosed environment and corpus, the warmed pooled path reaches
**zero measured steady-state allocations / zero bytes**. The default book's
allocation count is reported, not hidden.

### 6.2 Latency and throughput (local observations)

These are local, machine-dependent observations that accompany the allocation
experiment; they are not portable and not CI gates.

**Durham HPC — `high_cancellation_1m`, 1M × 5 (5,000,000 measured events):**

| path | p50 | p95 | p99 | p99.9 | max | throughput | allocations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| standard `OrderBook` | 281 ns | 501 ns | 631 ns | 812 ns | 207,601 ns | 2,879,594 ev/s | 7,340,580 |
| pooled `PooledOrderBook` | 291 ns | 491 ns | 681 ns | 832 ns | 213,673 ns | 2,815,909 ev/s | **0** |

On this 1M row latency is effectively a **tie** — the retained checksum/invariant
guardrails (§4) dominate this small-book workload, so removing allocations does
not move p50. The defensible result here is allocation removal under parity, and
the report says exactly that.

**Win/MSYS2 — `sample_hot_path_replay.bin`, 5,000 iters (after-change rows, shared guard `3714046084935619589`):**

| path | p50 | p95 | p99 | p99.9 | throughput | allocations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| standard `OrderBook` | 800 ns | 1,600 ns | 1,900 ns | 4,000 ns | 852,158.94 ev/s | 105,000 |
| pooled `PooledOrderBook` | 600 ns | 1,000 ns | 1,300 ns | 6,400 ns | 1,037,075.45 ev/s | **0** |

On this small fixture the pooled path shows both the allocation win **and** a
local latency/throughput improvement (p50 800 → 600 ns, ≈ +22% throughput).

**WSL2 — `high_cancellation_1m`, 1M × 5 (shared guard `14180005740461440914`):**

| path | p50 | p95 | p99 | p99.9 | max | throughput | allocations |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| standard `OrderBook` | 300 ns | 600 ns | 800 ns | 12,900 ns | 212,400 ns | 2,663,192 ev/s | 7,340,580 |
| pooled `PooledOrderBook` | 300 ns | 600 ns | 800 ns | 13,500 ns | 161,800 ns | 2,709,485 ev/s | **0** |

The Durham HPC small-stress table is the broadest single-environment latency view
of the change; representative rows (p50 / p99, std → pooled, allocations std →
pooled):

| Durham HPC corpus | events | std p50→pooled p50 | std p99→pooled p99 | std allocs → pooled |
| --- | ---: | --- | --- | --- |
| `stress_high_cancellation` | 20,000 | 250 → 200 ns | 421 → 380 ns | 29,390 → **0** |
| `stress_baseline_balanced` | 20,000 | 300 → 251 ns | 441 → 411 ns | 28,125 → **0** |
| `stress_replace_heavy` | 20,000 | 321 → 270 ns | 421 → 381 ns | 34,260 → **0** |
| `stress_deep_book` | 25,000 | 331 → 271 ns | 541 → 470 ns | 46,205 → **0** |
| `stress_wide_price_range` | 25,000 | 491 → 421 ns | 772 → 882 ns | 43,125 → **0** |

(The pooled path is lower on most of these small-stress rows, but `p99` is *higher*
on `stress_wide_price_range`; the latency deltas are local and not uniformly in
the pooled path's favour. Allocation removal is uniform.)

### 6.3 Inference-path allocation split (brief)

The inference work folded into the replay loop is **plumbing only** — it never
alters matching, strategy or risk, and makes **no** predictive/alpha/profitability
claim. The relevant performance point here is the allocation split: a caller-owned
feature buffer plus `LinearModel` scoring and the policy gate add **zero**
allocations on top of the node-based book.

**Durham HPC — `balanced_10k`, 50 iters (500,000 measured events):**

| row | p50 | p99 | throughput | allocations | guard |
| --- | ---: | ---: | ---: | ---: | --- |
| inference-free standard hot path | 371 ns | 601 ns | 2,132,930 ev/s | 703,450 | `892861025581029264` |
| pooled hot path | 310 ns | 541 ns | 2,507,441 ev/s | **0** | `892861025581029264` |
| LinearModel replay-loop inference | 511 ns | 792 ns | 1,649,716 ev/s | 703,450 | `1442857765714779360` |

The LinearModel event-loop row adds **≈ +140 ns p50** and **0 added allocations**
over the inference-free node-book row (its 703,450 count *is* the inference-free
count). The standalone inference micro-rows on the same Durham run
(`feature_extraction_caller_owned_buffer`, `linear_inference_only`,
`feature_buffer_measured_linear_inference`) all report **0 allocations**. The
optional ONNX Runtime backend was not built in this pass, so the ONNX replay-loop
row was recorded as skipped, not timed.

### 6.4 SPSC note (brief)

The opt-in bounded SPSC replay pipeline is orthogonal to the book change: it still
uses the **correctness-first node book**, so it is **not** an allocation-free
result (Durham reports 1.47M–2.55M allocations per 1M-event run). Its measured
value is correctness-under-concurrency, not allocation: on Durham HPC all six 1M
corpora are **lossless** (0 dropped), **checksum-identical** to single-thread
(parity true 6/6), and the bounded queue genuinely saturates (max depth = capacity
4096). The single/SPSC throughput ratio (0.70–1.23 on Durham) is a
systems-overhead observation, not a latency guarantee. A pooled-book SPSC variant
is future work (`ReplayEngine` is not templated on book type).

## 7. Correctness guardrails

The optimisation is only meaningful because it is **bit-for-bit equivalent** to
the default book. Parity is structural, not coincidental:

- **Guard-checksum parity.** Standard and pooled rows produce the **same** guard
  checksum on every measured corpus: `14180005740461440914` (Durham + WSL2
  high-cancellation 1M), `3714046084935619589` (Win/MSYS2 sample fixture),
  `892861025581029264` (pooled vs inference-free `balanced_10k` on Durham), and
  guard parity on 8/8 Durham stress corpora, 8/8 Win/MSYS2 stress corpora and 7/7
  Win/MSYS2 larger corpora.
- **C++ parity tests.** [tests/unit/test_pooled_order_book.cpp](../tests/unit/test_pooled_order_book.cpp)
  compares the standard and pooled paths for add, cancel, replace, partial/full
  reduce, FIFO/price-time priority, L2-view equivalence, final book-checksum
  equivalence and replay-checksum equivalence, including adversarial-but-valid
  lifecycles. CI checks this parity and the allocation behaviour; it does **not**
  gate on latency numbers.
- **Validation discipline.** Large-corpus throughput rows use `Light` validation,
  but correctness is independently covered by `ctest` `Full` validation and the
  end-of-replay checksum parity above.

In short: the pooled book changes *how* storage is laid out, not *what* the book
computes.

## 8. Remaining bottlenecks

The evidence points to costs the pooled book does **not** remove and to controls
this measurement campaign did not have:

- **Retained guardrail cost.** `append_to_checksum` / `fnv1a_append_byte` /
  `OrderBook::checksum` / `OrderBook::check_invariants` are large hotspot shares
  (≈ 26% checksum alone on the Durham high-cancellation hot path) and are *not*
  storage costs. They cap the latency benefit of allocation removal on small-book
  rows.
- **Memory locality / cache behaviour of the default book.** The default path is
  memory-latency-bound on deeper books (66–69% cache-miss-of-refs, IPC ≈ 0.73 on
  WSL2). A cache-friendlier default layout — or wider use of the pooled book — is
  the obvious next target.
- **Validation mode.** Per-run `Full` validation is O(book/event) and does not
  scale to 1M here, forcing `Light` for throughput rows.
- **Shared-HPC and virtualized-PMU noise.** Durham is one shared Slurm allocation
  with no LLC events and no root governor/turbo control; WSL2 is a virtualized PMU
  with multiplexed counters and uncontrolled turbo. Absolute numbers carry that
  noise.
- **No native/bare-metal controls.** No run yet has LLC cache events, a fixed
  governor/turbo, or a frame-pointer profiling build for flamegraph-quality call
  graphs. That pass would strengthen attribution but is not available here.
- **Scope of the pooled book.** It is single-symbol at the book layer and not
  wired into the SPSC pipeline; the multi-symbol-style corpus is skipped by the
  single-symbol hot-path benchmark.

## 9. Scope And Limitations

This case study deliberately does **not** claim, and nothing above should be read
as implying:

- portable performance or cross-machine/cross-cluster latency — all numbers are
  representative measurements on the stated host only;
- production-HFT performance, kernel bypass, FPGA or colocated networking;
- production readiness or production model serving (ONNX Runtime is opt-in and
  absent from default CI and from the Durham pass);
- live trading, authenticated exchange/broker connectivity or order placement;
- profitability, alpha, signal value or predictive quality — the inference path is
  **plumbing only** and the ChronosLOB models are trained on synthetic/recorded
  toy data with no trading significance;
- benchmark superiority over any other system.

Boundaries that stay fixed: **Durham HPC is the primary performance context**;
Windows/MSYS2 and WSL2 are **historical / local development baselines**; the
**pooled book is opt-in** and the **correctness-first `OrderBook` remains the
default**; benchmark numbers are **representative and environment-specific**.
Benchmarks are reported, never CI-gated. See [docs/claim_audit.md](claim_audit.md)
and [LIMITATIONS.md](../LIMITATIONS.md) for the full classification.

## 10. Reproduction pointers

Commands are reproducible; generated corpora, benchmark JSON and raw profiler
artefacts are git-ignored and never committed.

```bash
# Build the benchmark target (Release):
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_BENCHMARKS=ON
cmake --build build --target asterion_benchmarks

# Standard vs pooled hot path on the checked-in fixture (both rows in one pass):
./build/asterion_benchmarks --dataset data/samples/sample_hot_path_replay.bin \
  --hot-path-iterations 5000 --warmup-iterations 5 --json build/hot_path.json --no-text

# Larger-corpus standard-vs-pooled matrix (generates ignored corpora):
python scripts/run_perf_evaluation.py --warmup 5 --measure 30

# Generated stress corpora, standard vs pooled:
python scripts/run_pooled_stress_benchmarks.py --build-dir build \
  --hot-path-iterations 25 --warmup-iterations 5

# Linux perf counters/hotspots (fails loudly if perf/counters are unavailable):
scripts/run_linux_perf_profile.sh --dataset build/perf_corpora/baseline_1m.bin \
  --path pooled --iterations 30 --output-dir build/perf_profile
```

The exact per-environment commands, corpus manifests (with SHA-256), `perf` event
lists and full result tables live in the source reports linked at the top of this
document and in [BENCHMARKS.md](../BENCHMARKS.md) /
[docs/profiling.md](profiling.md).
