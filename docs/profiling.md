# Profiling

Profiling results are hardware, kernel, compiler and power-policy dependent. Keep raw outputs local unless they are clearly labelled with environment metadata.

## Linux perf

Build Release first:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_BENCHMARKS=ON
cmake --build build --target asterion_benchmarks
```

Collect counters:

```bash
perf stat -e cycles ./build/asterion_benchmarks --no-text --json build/bench.json
perf stat -e cache-misses ./build/asterion_benchmarks --no-text --json build/bench.json
perf stat -e branch-misses ./build/asterion_benchmarks --no-text --json build/bench.json
perf stat -e cycles,instructions,cache-references,cache-misses,branches,branch-misses ./build/asterion_benchmarks
```

Record flamegraph-ready samples:

```bash
perf record -F 997 -g -- ./build/asterion_benchmarks
perf script > build/perf.script
```

If Brendan Gregg's FlameGraph scripts are installed:

```bash
stackcollapse-perf.pl build/perf.script > build/out.folded
flamegraph.pl build/out.folded > build/asterion.svg
```

## Google Benchmark JSON

```bash
cmake -S . -B build-gbench -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_GOOGLE_BENCHMARK=ON
cmake --build build-gbench --target asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks --benchmark_format=json > build-gbench/google_benchmark.json
```
