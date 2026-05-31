# Perf Profile

Representative local measurements on this machine, not portable performance claims.

## Local Status

Linux `perf` was not run in the current environment.

Blocker:

- The active shell is Windows PowerShell.
- `perf` is not available on `PATH`.
- `wsl.exe` is present, but invoking `wsl -e sh -lc 'command -v perf || true'` returned the WSL
  usage text instead of launching a Linux distribution, so there is no usable local Linux `perf`
  environment from this shell.

No `perf stat` counters or flamegraph samples are fabricated in this report.

## Linux Commands

The helper script below builds the benchmark target, runs `perf stat -d`, records call stacks and
renders a flamegraph when Brendan Gregg's FlameGraph scripts are installed:

```bash
BUILD_DIR=build-perf \
HOT_PATH_ITERATIONS=10000 \
DATASET="$PWD/data/samples/sample_hot_path_replay.bin" \
./scripts/profile_hot_path_perf.sh
```

Equivalent manual commands:

```bash
cmake -S . -B build-perf -G Ninja \
  -DCMAKE_BUILD_TYPE=Release \
  -DASTERION_BUILD_TESTS=OFF \
  -DASTERION_BUILD_BENCHMARKS=ON \
  -DASTERION_BUILD_PYTHON=OFF
cmake --build build-perf --target asterion_benchmarks

perf stat -d -r 5 \
  ./build-perf/asterion_benchmarks \
  --dataset data/samples/sample_hot_path_replay.bin \
  --hot-path-iterations 10000 \
  --json build-perf/hot_path_benchmark.json \
  --no-text

perf record -F 999 -g -- \
  ./build-perf/asterion_benchmarks \
  --dataset data/samples/sample_hot_path_replay.bin \
  --hot-path-iterations 10000 \
  --no-text
perf script | stackcollapse-perf.pl > build-perf/hot_path.folded
flamegraph.pl build-perf/hot_path.folded > build-perf/hot_path_flamegraph.svg
```

Keep raw `perf.data`, folded stacks and SVGs local unless they are intentionally curated with
environment metadata.
