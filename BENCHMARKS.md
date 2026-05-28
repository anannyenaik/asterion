# Benchmarks

Asterion includes a local benchmark runner for repeatable experiments. It reports timings for the current machine only; do not treat results as portable performance claims.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_BENCHMARKS=ON
cmake --build build --target asterion_benchmarks
```

## Run

```bash
./build/asterion_benchmarks
./build/asterion_benchmarks --dataset data/samples/sample_replay.csv
./build/asterion_benchmarks --json build/asterion_benchmark.json --no-text
```

The JSON schema is stable and includes:

- `schema_version`;
- `environment`: CPU, OS, compiler, build type, compiler flags, commit hash, dataset and logging mode;
- `benchmarks`: name, iterations, total/average nanoseconds, guard checksum and allocation counters.

The runner currently covers:

- add order;
- cancel order;
- replace order;
- market order crossing one level;
- market order crossing multiple levels;
- L2 snapshot generation;
- replay of sample events;
- risk check only;
- linear inference only.

## Optional Google Benchmark

Google Benchmark is opt-in so normal CI and local builds do not depend on fetching it.

```bash
cmake -S . -B build-gbench -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_GOOGLE_BENCHMARK=ON
cmake --build build-gbench --target asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks --benchmark_format=json > build-gbench/google_benchmark.json
```

## Optional CI Hook

The `benchmarks` GitHub Actions workflow is manual-only (`workflow_dispatch`). It builds the benchmark target and validates JSON shape, but it does not fail normal CI on performance variance.

## Replay Corpora

Generate local corpora under `data/generated/`, which is ignored by git:

```bash
python scripts/generate_synthetic_events.py --mode balanced --events 1000 --output data/generated/balanced.csv
python scripts/generate_synthetic_events.py --mode high-cancel --events 1000 --output data/generated/high_cancel.csv
python scripts/generate_synthetic_events.py --mode deep-book --events 1000 --output data/generated/deep_book.csv
python scripts/generate_synthetic_events.py --mode bursty --events 1000 --output data/generated/bursty.csv
python scripts/generate_synthetic_events.py --mode multi-symbol --symbols 4 --events 1000 --output data/generated/multi_symbol.csv
python scripts/generate_synthetic_events.py --mode wide-price-range --events 1000 --output data/generated/wide_range.csv
```

## Methodology

- Build in Release mode.
- Run on an otherwise quiet machine.
- Record compiler, CPU, OS, governor/power settings and commit hash.
- Run multiple samples and compare distributions, not isolated best cases.
- Keep generated result files out of git unless they are clearly sample-format fixtures.
