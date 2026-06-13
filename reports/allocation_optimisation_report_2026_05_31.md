# Allocation Optimisation Report - 2026-05-31

This report measures the opt-in pooled L3 path under the disclosed local
environment. The correctness-first `OrderBook` remains the default, and all
timings are environment-specific.

## Scope

Measured path:

```text
binary replay -> L3 book update -> reusable L2 view -> fixed-size strategy callback -> risk check
```

The experiment targets L3 replay allocations after warm-up. Its scope is
allocation behavior and local systems cost for recorded replay.

## Environment

- OS: Windows
- CPU: Intel64 Family 6 Model 158 Stepping 9, GenuineIntel
- Compiler: GCC 16.1.0 from MSYS2 UCRT64
- Build type: Release
- Compiler flags: `-O3 -DNDEBUG`
- CMake: 4.3.3
- Ninja: 1.13.2
- Python: 3.14.5
- Dataset: `data/samples/sample_hot_path_replay.bin`
- Dataset size: 12 events
- Benchmark iterations: 5,000 replays
- Measured event samples: 60,000
- Strategy-driven risk checks: 55,000
- Warm-up policy: 5 replay iterations before timing and allocation counters are reset
- Benchmark executable commit metadata: `54f2a1d0e11b`

## Previous Allocation Source

The correctness-first hot-path benchmark rebuilt the L3 book every replay pass. Even with the L2
view, strategy callback and risk accepted-ID path warmed and reserved, Add and Replace replay still
allocated standard-library nodes for the default book: price-level map nodes, FIFO list nodes and
node-based order-index entries. Those allocations are deterministic and visible in the measured
allocation counter.

## Design Chosen

`PooledOrderBook` is a separate opt-in L3 implementation for the benchmark path. It uses:

- vector-backed bid/ask price levels, sorted best-first;
- vector-backed order nodes linked into FIFO queues by index;
- a reusable open-addressed order-id index;
- explicit `reserve_order_capacity(...)` warm-up before measured replay;
- the same public book operations used by replay and L2 projection.

The default `OrderBook` is not rewritten. Benchmark output now includes both:

- `hot_path_binary_replay_l3_l2_strategy_risk`
- `hot_path_binary_replay_pooled_l3_l2_strategy_risk`

## Correctness Parity

New C++ tests compare the stable and pooled paths for add, cancel, replace, partial reduce, full
reduce, FIFO/price-time priority, L2 view equivalence, final book checksum equivalence and replay
checksum equivalence.

Replay parity covers:

- `data/samples/sample_replay.csv`
- `data/samples/sample_replay.bin`
- `data/samples/sample_hot_path_replay.bin`
- `data/samples/binance_depth_sample.normalised.bin`

CTest result during the implementation run: `1/1` test executable passed.

## Local Measurements

| benchmark | p50 | p95 | p99 | p99.9 | max | throughput | allocations | bytes |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| before: `hot_path_binary_replay_l3_l2_strategy_risk` | 800 ns | 1,300 ns | 1,500 ns | 3,300 ns | 2,455,800 ns | 861,102.70 events/s | 105,000 | 6,720,000 |
| after: `hot_path_binary_replay_l3_l2_strategy_risk` | 800 ns | 1,600 ns | 1,900 ns | 4,000 ns | 54,200 ns | 852,158.94 events/s | 105,000 | 6,720,000 |
| after: `hot_path_binary_replay_pooled_l3_l2_strategy_risk` | 600 ns | 1,000 ns | 1,300 ns | 6,400 ns | 4,791,400 ns | 1,037,075.45 events/s | 0 | 0 |

Both after rows produced the same guard checksum: `3714046084935619589`.

The primary result is the allocation reduction from 105,000 measured allocations to 0 for the
opt-in pooled path after warm-up. The latency rows are local observations from one machine and should
not be treated as portable performance claims.

## Remaining Allocations

The correctness-first benchmark path still reports 105,000 allocations for this workload because it
continues to use the stable node-based `OrderBook`. Warm-up allocations for `PooledOrderBook`
reservation are intentionally outside the measured loop. Other benchmarks may still allocate, for
example feature extraction intentionally returns a vector.

## Stress-Corpus Follow-Up

A second pass extended the same opt-in pooled path across larger generated corpora and adversarial
valid lifecycle streams. See
`reports/pooled_order_book_stress_report_2026_05_31.md` for the full dataset table.

Summary of the local stress run:

| dataset | events | standard allocations | pooled allocations | pooled bytes | guard match |
|---|---:|---:|---:|---:|---|
| baseline_balanced | 4,000 | 140,625 | 0 | 0 | yes |
| high_cancellation_rate | 4,000 | 146,950 | 0 | 0 | yes |
| replace_heavy | 4,000 | 171,300 | 0 | 0 | yes |
| deep_book | 5,000 | 231,025 | 0 | 0 | yes |
| wide_price_range | 5,000 | 215,625 | 0 | 0 | yes |
| bursty_flow | 4,000 | 141,375 | 0 | 0 | yes |
| long_running_same_symbol | 8,000 | 259,475 | 0 | 0 | yes |
| adversarial_lifecycle | 4,000 | 150,000 | 0 | 0 | yes |

The multi-symbol-style generated corpus is produced under the ignored stress directory, but the
single-symbol hot-path benchmark intentionally skips it. CI parity tests group that stream by symbol
for pooled-vs-standard book comparison.

## Reproduce

```powershell
$env:PATH='C:\msys64\ucrt64\bin;C:\msys64\usr\bin;' + $env:PATH
cmake -S . -B build-allocation-after -G Ninja `
  -DCMAKE_BUILD_TYPE=Release `
  -DASTERION_ENABLE_WARNINGS=ON `
  -DASTERION_BUILD_PYTHON=ON `
  -DASTERION_BUILD_TESTS=ON `
  -DASTERION_BUILD_BENCHMARKS=ON `
  -DPython3_EXECUTABLE=C:/msys64/ucrt64/bin/python.exe
cmake --build build-allocation-after --target asterion_tests asterion_benchmarks
ctest --test-dir build-allocation-after --output-on-failure
.\build-allocation-after\asterion_benchmarks.exe `
  --dataset data\samples\sample_hot_path_replay.bin `
  --hot-path-iterations 5000 `
  --warmup-iterations 5 `
  --json build-allocation-after\after_hot_path_2026_05_31.json `
  --no-text
```

## Limitations

- `PooledOrderBook` is opt-in and scoped to the measured L3 replay path.
- It is not a production allocator, not a live-trading component and not a latency portability claim.
- The zero-allocation result depends on explicit warm-up/reservation and the disclosed dataset.
- Default CI should run correctness and allocation-behaviour tests, not benchmark-number gates.
