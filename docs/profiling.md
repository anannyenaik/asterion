# Profiling

Profiling results are hardware, kernel, compiler and power-policy dependent. Keep raw outputs local unless they are clearly labelled with environment metadata.

## Linux perf

Build Release first:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_BENCHMARKS=ON
cmake --build build --target asterion_benchmarks
```

Generate a deterministic local corpus and run the standard-vs-pooled comparison:

```bash
python scripts/run_perf_evaluation.py --datasets baseline_1m --warmup 5 --measure 30
```

Then use the Linux-only helper. It writes plain-text output under the git-ignored
`build/perf_profile/` directory and never invents results if `perf` or hardware counters are
unavailable.

```bash
scripts/run_linux_perf_profile.sh --dataset build/perf_corpora/baseline_1m.bin \
    --path pooled --iterations 30 --output-dir build/perf_profile
```

Collect counters directly:

```bash
perf stat -d ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin \
    --only-hot-path --hot-path-iterations 30 --no-text
perf stat -e cycles,instructions,branches,branch-misses,cache-references,cache-misses,context-switches,page-faults \
    ./build/asterion_benchmarks --dataset build/perf_corpora/baseline_1m.bin \
    --only-hot-path --hot-path-iterations 30 --no-text
```

The benchmark JSON contains both L3 book variants:

- `hot_path_binary_replay_l3_l2_strategy_risk`
- `hot_path_binary_replay_pooled_l3_l2_strategy_risk`

Compare allocation counters only after the configured warm-up has completed. Latency counters are
local observations and should not be used as CI gates.

Record flamegraph-ready samples:

```bash
perf record -F 99 -g -- ./build/asterion_benchmarks \
    --dataset build/perf_corpora/baseline_1m.bin --only-hot-path --hot-path-iterations 30
perf report
perf script > build/perf_profile/perf.script
```

If Brendan Gregg's FlameGraph scripts are installed:

```bash
stackcollapse-perf.pl build/perf_profile/perf.script > build/perf_profile/out.folded
flamegraph.pl build/perf_profile/out.folded > build/perf_profile/asterion.svg
```

GitHub-hosted runners often do not expose hardware performance counters. The manual
`linux-performance` workflow records that blocker honestly and does not gate on benchmark numbers.

## Google Benchmark JSON

```bash
cmake -S . -B build-gbench -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_GOOGLE_BENCHMARK=ON
cmake --build build-gbench --target asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks --benchmark_format=json > build-gbench/google_benchmark.json
```
