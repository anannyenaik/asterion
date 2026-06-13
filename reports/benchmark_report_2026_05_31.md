# Benchmark Report - 2026-05-31

This report records representative local measurements for the disclosed machine
and workload.

Update: a later same-day allocation experiment added the opt-in pooled L3 benchmark path. See
`reports/allocation_optimisation_report_2026_05_31.md` for the before/after allocation comparison.

## Scope

Measured path: binary event replay -> L3 book update -> reusable L2 view generation -> fixed-size
imbalance strategy callback -> risk check.

The measurements cover recorded replay systems cost. They are not portable
across machines and carry no live-trading or profitability claim.

## Environment

- Source commit measured by the benchmark executable: `47f30027c24a`
- OS: Windows
- CPU: Intel64 Family 6 Model 158 Stepping 9, GenuineIntel
- Compiler: GCC 16.1.0 from MSYS2 UCRT64
- Build type: Release
- Compiler flags reported by CMake: `-O3 -DNDEBUG`
- Logging mode: `stdout+json`; no logging or string formatting occurs inside the measured event loop
- Toolchain: CMake 4.3.3, Ninja 1.13.2

## Dataset

- Dataset: `data/samples/sample_hot_path_replay.bin`
- Format: fixed 58-byte binary market-data records with the `ASTITCH1` header
- Dataset size: 12 events
- Benchmark iterations: 10,000 replays
- Measured event samples: 120,000
- Strategy-driven risk checks: 110,000
- Warm-up policy: 5 replay iterations before allocation counters and timing samples are reset

## Command

```powershell
$env:Path='C:\msys64\ucrt64\bin;' + $env:Path
.\build-hotpath-after\asterion_benchmarks.exe `
  --json build-hotpath-after\representative_benchmark_2026_05_31.json `
  --hot-path-iterations 10000
```

## Target Hot-Path Result

| metric | value |
|---|---:|
| p50 | 800 ns |
| p95 | 1,400 ns |
| p99 | 2,000 ns |
| p99.9 | 7,500 ns |
| max | 173,300 ns |
| avg wall time per event | 1,235 ns |
| throughput | 809,066.40 events/s |
| allocation count | 210,000 |
| bytes allocated | 13,440,000 |
| guard checksum | 18052214259513584877 |

The remaining allocations are expected for this implementation: the measured binary replay includes
Add and Replace events, and the current L3 book stores price levels and FIFO queues in standard
`std::map`/`std::list` nodes. Those node allocations are still in the measured path. The reusable L2
view, fixed-size strategy callback and reserved risk accepted-ID path are allocation-free after
warm-up in deterministic tests.

## Before / After

The dedicated end-to-end hot-path benchmark did not exist before this change, so there is no valid
before/after number for that exact pipeline.

Measurable sub-paths from the local baseline build:

| benchmark | baseline | current |
|---|---:|---:|
| `l2_snapshot_generation` avg | 1,923 ns | 293 ns |
| `l2_snapshot_generation` allocations | 600,000 | 0 |
| `risk_check_only` allocations | 50,013 | 27 |

Baseline numbers came from the pre-change `build-hotpath-before` release benchmark on this same
laptop. Current numbers came from the `47f30027c24a` benchmark executable. Timing deltas are local
observations only; the allocation deltas are the defensible result.
