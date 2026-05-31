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
./build/asterion_benchmarks --dataset data/samples/sample_replay.bin
./build/asterion_benchmarks --dataset data/samples/sample_hot_path_replay.bin --hot-path-iterations 10000
./build/asterion_benchmarks --json build/asterion_benchmark.json --no-text
PYTHONPATH=build/python python python/examples/load_benchmark_json.py build/asterion_benchmark.json
```

The JSON schema is stable and includes:

- `schema_version`;
- `environment`: CPU, OS, compiler, build type, compiler flags, commit hash, dataset and logging mode;
- `benchmarks`: name, `category` (`core` or `inference`), `backend`, `model_name`, `input_shape`,
  iterations, warm-up iterations, measured iterations, event count, optional p50/p95/p99/p99.9/max
  latency fields, throughput, guard checksum and allocation counters.

Benchmarks are split into two categories so inference timings are never folded into the trading hot
path. The text output prints a `# core` group and a `# inference` group; the JSON tags every row with
`category`.

Core (replay / book / matching / risk):

- binary replay -> L3 book update -> reusable L2 view -> fixed-size imbalance strategy callback ->
  risk check (`hot_path_binary_replay_l3_l2_strategy_risk`);
- the same binary replay path through the opt-in pooled L3 book
  (`hot_path_binary_replay_pooled_l3_l2_strategy_risk`);
- add order;
- cancel order;
- replace order;
- market order crossing one level;
- market order crossing multiple levels;
- L2 snapshot generation;
- replay of sample events;
- risk check only.

Inference (feature extraction / model scoring, measured separately, `timing_mode = per-call` so each
row carries a real p50/p95/p99/p99.9/max distribution):

- feature extraction only (`feature_extraction_only`);
- LinearModel inference only (`linear_inference_only`);
- feature extraction + LinearModel (`feature_extraction_plus_linear_inference`);
- measured-engine path: model score + timeout/late-signal policy accounting
  (`measured_linear_inference_only`);
- event-loop policy-gate overhead with injected timings (`inference_policy_overhead`);
- ONNX inference only (`onnx_inference_only`) — **only when built with ONNX Runtime**;
- feature extraction + ONNX (`feature_extraction_plus_onnx_inference`) — **only when built with ONNX
  Runtime**.

Each inference row records its `backend` (`linear`, `onnx`, or `n/a`), `model_name` and `input_shape`
(`1x4`). For sub-microsecond operations the per-call p50 is dominated by the timer resolution (~100 ns
on the reference machine), not by the operation; the curated inference report calls this out and cites
the aggregate throughput as the better estimate of raw op cost. Feature extraction allocates one
`std::vector<double>` per call; LinearModel scoring and the policy gate allocate nothing after warm-up.
ONNX inference allocations are measured and reported honestly, not asserted to be zero. See
[reports/inference_report_2026_05_31.md](reports/inference_report_2026_05_31.md).

The hot-path benchmark defaults to `data/samples/sample_hot_path_replay.bin`, a small checked-in
binary fixture with both sides of book so the strategy callback emits decisions and the risk gateway
is exercised. The benchmark warms the path before resetting timing and allocation counters. The
correctness-first path avoids heap allocation in reusable L2 generation, fixed strategy decisions
and reserved risk client-ID tracking after warm-up. Add/Replace events still allocate in the default
L3 book because it uses standard node-based containers for price levels, FIFO queues and lookup
entries; those remaining allocations are reported rather than hidden.

The pooled L3 benchmark is opt-in and uses `PooledOrderBook`, a vector-backed price-level/order-node
book with a reusable flat order-id index. It shares the same replay semantics in parity tests but is
not the default replay or matching book. It exists to measure the allocation-reduction experiment
under disclosed warm-up conditions.

A curated local report is checked in at
`reports/benchmark_report_2026_05_31.md`. It is representative local evidence from one laptop, not a
portable performance claim.
The pooled-book allocation experiment is reported in
`reports/allocation_optimisation_report_2026_05_31.md`.

The benchmark runner is not extended with performance claims for shared replay, persistent audit
logging/rotation/manifests, sliding-window rate limiting, replace-risk checks, simulated
broker/session lifecycle, simulated portfolio risk, cancel-on-kill or cancel-on-disconnect. Those
paths are covered by correctness and smoke tests; any local measurements remain machine-dependent
artifacts. The optional ONNX Runtime path is benchmarked only in the opt-in ONNX build, and its
results are explicitly representative-local, plumbing-only measurements (no model-quality claim).

## Optional Google Benchmark

Google Benchmark is opt-in so normal CI and local builds do not depend on fetching it.

```bash
cmake -S . -B build-gbench -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_GOOGLE_BENCHMARK=ON
cmake --build build-gbench --target asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks --benchmark_format=json > build-gbench/google_benchmark.json
```

## Regression Comparison

The offline comparison tool compares two benchmark JSON files and reports per-benchmark percentage
changes, new and missing benchmarks, and threshold breaches. The threshold and the compared metric
(`avg_ns`, `total_ns` or `iterations`) are configurable.

```bash
python scripts/asterion_inspect.py benchmark-compare \
  --baseline build/baseline.json \
  --current build/current.json \
  --threshold-pct 10 --metric avg_ns --json
```

The command exits non-zero only when `--fail-on-regression` is passed, so normal runs never fail on
performance variance.

**Benchmark regression results are machine-dependent.** Comparing two JSON files is only meaningful
when both were produced on the same controlled hardware under comparable conditions (same compiler,
build type, CPU governor and an otherwise quiet machine). Cross-machine comparison numbers are not
meaningful. Generic benchmark dumps are not checked into this repository; the curated report under
`reports/` is explicitly labelled as representative local evidence. `data/samples/sample_benchmark_*.json`
files are synthetic tooling fixtures used only to exercise the comparison logic, not measurements.

## Historical Store And Trends

Benchmark JSON can be stored locally and compared across runs. History lives under
`benchmarks/history/`, which is git-ignored, so no numbers are committed. Trend reporting reuses the
benchmark schema and the regression metric selection (`avg_ns`, `total_ns` or `iterations`).

```bash
python scripts/asterion_inspect.py benchmark-store \
  --input build/asterion_benchmark.json --history-dir benchmarks/history
python scripts/asterion_inspect.py benchmark-trend \
  --history-dir benchmarks/history --metric avg_ns --json
# Or trend an explicit, ordered list of files:
python scripts/asterion_inspect.py benchmark-trend \
  --inputs run1.json run2.json run3.json --json
```

`benchmark-store` validates the JSON and, without `--name`, writes a sequential
`benchmark_NNNN.json`. `benchmark-trend` reports, per benchmark, the first/last/min/max value, mean
and first-to-last percentage change across the supplied runs.

**Trends are only meaningful on controlled hardware.** They are comparable solely when every stored
run was produced on the same machine under comparable conditions (same compiler, build type, CPU
governor and an otherwise quiet machine). This tooling is intentionally kept out of CI performance
gates; it never fails a build.

## Latency Budget

`asterion_latency_budget` measures the tick-to-trade stages and accounts them against configurable
budgets:

```bash
cmake --build build --target asterion_latency_budget
./build/asterion_latency_budget --iterations 1000
./build/asterion_latency_budget --risk-budget-ns 500 --json build/latency_budget.json --no-text
python scripts/asterion_inspect.py latency-budget --input build/latency_budget.json --json
```

Budgets default to `0` (unset): stages are still measured but never flagged as exceeded. There are
no default latency targets because realistic budgets depend on hardware and workload. The configuration
checksum is deterministic; the observed nanoseconds are machine-dependent and are not portable
performance claims.

## Optional CI Hook

The `benchmarks` GitHub Actions workflow is manual-only (`workflow_dispatch`). It builds the
benchmark and latency-budget targets, validates JSON shape, and runs an informational two-run
benchmark comparison and latency-budget summary. It does not fail normal CI on performance variance.

## Replay Corpora

Generate local corpora under `data/generated/`, which is ignored by git:

```bash
python scripts/generate_synthetic_events.py --mode balanced --events 1000 --output data/generated/balanced.csv
python scripts/generate_synthetic_events.py --mode balanced --events 1000 --output data/generated/balanced.bin
python scripts/generate_synthetic_events.py --mode high-cancel --events 1000 --output data/generated/high_cancel.csv
python scripts/generate_synthetic_events.py --mode replace-heavy --events 1000 --output data/generated/replace_heavy.csv
python scripts/generate_synthetic_events.py --mode deep-book --events 1000 --output data/generated/deep_book.csv
python scripts/generate_synthetic_events.py --mode bursty --events 1000 --output data/generated/bursty.csv
python scripts/generate_synthetic_events.py --mode long-same-symbol --events 1000 --output data/generated/long_same_symbol.csv
python scripts/generate_synthetic_events.py --mode multi-symbol --symbols 4 --events 1000 --output data/generated/multi_symbol.csv
python scripts/generate_synthetic_events.py --mode wide-price-range --events 1000 --output data/generated/wide_range.csv
python scripts/generate_synthetic_events.py --mode adversarial-lifecycle --events 1000 --output data/generated/adversarial_lifecycle.csv
python scripts/convert_event_log.py --input data/generated/balanced.csv --output data/generated/balanced_converted.bin
```

For the opt-in pooled L3 path, the helper below generates larger ignored corpora and runs the
standard and pooled hot-path benchmarks against each single-symbol dataset:

```bash
python scripts/run_pooled_stress_benchmarks.py \
  --build-dir build \
  --hot-path-iterations 100 \
  --warmup-iterations 5
```

It writes generated logs under `data/generated/pooled_order_book_stress/`, per-dataset benchmark
JSON under `benchmarks/results/pooled_order_book_stress/`, and a compact
`pooled_order_book_stress_summary.json`. The generated multi-symbol-style corpus is recorded but not
fed to the single-symbol hot-path benchmark; pooled multi-symbol replay is outside the current
benchmark architecture.

The replay benchmark uses recorded/simulated event logs only. It does not imply live exchange
connectivity or portable hardware performance.

## Python Analysis

The Python package can load benchmark JSON and return a compact schema-aware summary:

```python
import asterion

summary = asterion.summarise_benchmark_json("build/asterion_benchmark.json")
print(summary["benchmark_count"])
```

`data/samples/sample_benchmark_schema.json` is a schema-only fixture for examples. It intentionally
contains no benchmark numbers.

## Methodology

- Build in Release mode.
- Run on an otherwise quiet machine.
- Record compiler, CPU, OS, governor/power settings and commit hash.
- Run multiple samples and compare distributions, not isolated best cases.
- Keep generated result files out of git unless they are clearly sample-format fixtures.
- Keep curated reports explicit about warm-up, logging mode, dataset size, allocation count and
  remaining allocation sources.
