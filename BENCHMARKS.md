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
./build/asterion_benchmarks --only-steady-state-replay --dataset data/generated/balanced_100k.bin --steady-state-validation-mode light
./build/asterion_benchmarks --json build/asterion_benchmark.json --no-text
PYTHONPATH=build/python python python/examples/load_benchmark_json.py build/asterion_benchmark.json
```

The JSON schema is stable and includes:

- `schema_version`;
- `environment`: CPU, OS, compiler, build type, compiler flags, commit hash, dataset and logging mode;
- `benchmarks`: name, `category` (`core` or `inference`), `backend`, `model_name`, `input_shape`,
  `output_shape`, optional `feature_count` and `feature_version`, iterations, warm-up iterations, measured
  iterations, event count, validation mode, thread lifecycle mode, optional p50/p95/p99/p99.9/max
  latency fields, throughput, replay checksums, guard checksum and allocation counters.
- optional `skipped_benchmarks`: name, category, requested backend, model name and reason for
  optional benchmark rows that were unavailable in the current build/environment. Skipped rows are
  not numeric measurements and are intentionally outside the `benchmarks` array.

Benchmarks are split into two categories so inference timings are never folded into the trading hot
path. The text output prints a `# core` group and a `# inference` group; the JSON tags every row with
`category`.

Core (replay / book / matching / risk):

- binary replay -> L3 book update -> reusable L2 view -> fixed-size imbalance strategy callback ->
  risk check (`hot_path_binary_replay_l3_l2_strategy_risk`);
- the same binary replay path through the opt-in pooled L3 book
  (`hot_path_binary_replay_pooled_l3_l2_strategy_risk`);
- single-thread deterministic replay (book + validation + diagnostics) as the SPSC parity baseline
  (`replay_l3_diagnostics_single_thread`);
- the opt-in bounded SPSC replay pipeline over the same path
  (`spsc_replay_l3_diagnostics`). This row also reports `queue_capacity`, `produced_events`,
  `consumed_events`, `backpressure_count`, `dropped_events`, `max_queue_depth` and
  `checksum_parity` (whether the threaded result matched the single-thread baseline bit-for-bit).
  These two rows use `timing_mode = per-run`: each latency sample is one whole-dataset replay run,
  and each SPSC run spins up and joins a producer thread (thread lifecycle is part of the measured
  cost), so per-run latency on a tiny dataset is dominated by thread setup. See
  [reports/spsc_replay_pipeline_report_2026_05_31.md](reports/spsc_replay_pipeline_report_2026_05_31.md);
- steady-state replay rows for large-corpus SPSC evaluation:
  `single_thread_replay_steady_state_l3_diagnostics` and
  `spsc_replay_steady_state_l3_diagnostics`. These rows use `timing_mode = aggregate`, expose
  `thread_lifecycle_mode` (`single_thread` or `steady_state`) and `validation_mode`, and are intended
  for throughput evaluation where total elapsed time and throughput are more meaningful than
  per-run latency percentiles. The steady SPSC row creates producer and consumer threads once, starts
  timing after both are ready, streams the preloaded corpus, reports EOS marker counts and keeps
  checksum parity against the matching single-thread row. Use `--only-steady-state-replay` for large
  local corpora so old full-validation per-run rows do not pollute the measurement. See
  [reports/spsc_steady_state_report_2026_05_31.md](reports/spsc_steady_state_report_2026_05_31.md);
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

- vector-returning feature extraction (`feature_extraction_vector_returning`);
- caller-owned-buffer feature extraction (`feature_extraction_caller_owned_buffer`);
- LinearModel inference only (`linear_inference_only`);
- vector-returning feature extraction + LinearModel
  (`feature_extraction_plus_linear_vector_returning`);
- caller-owned-buffer feature extraction + LinearModel
  (`feature_extraction_plus_linear_caller_owned_buffer`);
- measured-engine path: model score + timeout/late-signal policy accounting
  (`measured_linear_inference_only`);
- caller-owned-buffer feature extraction + measured LinearModel/policy path
  (`feature_buffer_measured_linear_inference`);
- event-loop policy-gate overhead with injected timings (`inference_policy_overhead`);
- caller-owned-buffer feature extraction + policy-gate overhead
  (`feature_buffer_policy_gate_overhead`);
- full event-loop inference path: binary replay → L3 book update → reusable L2 view →
  caller-owned feature extraction → LinearModel → measured timeout/late-signal policy
  gate, alongside the existing strategy + risk path
  (`hot_path_binary_replay_l3_l2_inference_strategy_risk`). This is a per-event row
  (`timing_mode = per-event`) on the hot-path dataset; it measures the *added systems
  cost* of inserting synchronous inference into the deterministic event loop. The
  model score and policy decision are folded into the guard checksum so the stage is
  not optimised away, but they never alter matching, strategy or risk behaviour — no
  decisioning, alpha or profitability claim. The caller-owned inference stage adds
  **0** steady-state allocations on top of the node-based book, so this row's
  allocation count matches the inference-free `hot_path_binary_replay_l3_l2_strategy_risk`
  row. See [reports/inference_event_loop_cost_report_2026_06_01.md](reports/inference_event_loop_cost_report_2026_06_01.md);
- ChronosLOB ONNX suites — **only when built with ONNX Runtime** — emitted twice,
  once for the legacy `Gemm` fixture (`chronoslob_fixture` label) and once for the
  real trained `DeepLOBModel` (`chronoslob_real` label):
  - `<label>_onnx_model_load` (model-load timing, one-time),
  - `<label>_onnx_inference_only`,
  - `feature_extraction_plus_<label>_onnx_vector_returning`,
  - `feature_extraction_plus_<label>_onnx_caller_owned_buffer`,
  - `feature_buffer_measured_<label>_onnx_inference` (policy-gated path).
- optional full replay-loop + real ChronosLOB ONNX row:
  `hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk`.
  This row is emitted only when the active backend is actually ONNX. Default
  builds report it under `skipped_benchmarks` instead of silently timing the
  `LinearModel` fallback under an ONNX name. It measures replay -> L3/L2 update
  -> caller-owned feature extraction -> real tiny ChronosLOB ONNX scoring ->
  measured policy gate -> strategy/risk/replay accounting, with model load/setup
  measured separately by `chronoslob_real_onnx_model_load`.

Each inference row records its `backend` (`linear`, `onnx`, or `n/a`), `model_name`, `input_shape`
and `output_shape` (`1x4`/`1x1` for the fixture, `1x1x4`/`1x3` for the real DeepLOB), and feature
metadata (`feature_count=4`, `feature_version=1`) when the row
uses the L2 feature schema. For sub-microsecond operations the per-call p50 is dominated by timer
resolution, not by the operation; the curated inference report calls this out and cites aggregate
throughput as the better estimate of raw op cost. The vector-returning extraction path intentionally
allocates one `std::vector<double>` per call. The caller-owned-buffer extraction path, LinearModel
scoring and the policy gate are measured separately and are expected to allocate nothing after
warm-up in the scoped unit tests/benchmark rows. ONNX inference allocations are measured and reported
honestly, not asserted to be zero. See
[reports/inference_report_2026_05_31.md](reports/inference_report_2026_05_31.md). The
ChronosLOB ONNX bridge is documented in
[docs/chronoslob_bridge.md](docs/chronoslob_bridge.md), with the legacy fixture in
[reports/chronoslob_onnx_bridge_report_2026_05_31.md](reports/chronoslob_onnx_bridge_report_2026_05_31.md)
and the **real trained DeepLOB artefact** in
[reports/chronoslob_real_model_bridge_report_2026_06_01.md](reports/chronoslob_real_model_bridge_report_2026_06_01.md).
Representative local measurements (this machine/environment), not portable claims: the real DeepLOB
inference p50 ≈ 29 µs (p99 ≈ 66 µs) vs the fixture `Gemm` ≈ 7 µs, both well above the
zero-allocation `LinearModel`; ONNX steady-state shows ~2 allocations/call (ONNX Runtime per-run
buffers), separated from one-time model-load allocations.
The feature-buffer-specific report is
[reports/inference_feature_buffer_report_2026_05_31.md](reports/inference_feature_buffer_report_2026_05_31.md).

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

Replay validation defaults to correctness-first `full`: after every event, replay checks full book
invariants and crossed top of book. The benchmark-only `--steady-state-validation-mode light` option
keeps cheap per-event top-of-book checks and defers the full invariant walk to end-of-replay. This is
explicitly for large-corpus throughput evaluation; it does not weaken the default replay path or the
correctness tests.

A curated local report is checked in at
`reports/benchmark_report_2026_05_31.md`. It is representative local evidence from one laptop, not a
portable performance claim.
The pooled-book allocation experiment is reported in
`reports/allocation_optimisation_report_2026_05_31.md`.

For a single cross-report view of all performance/allocation evidence — what it proves, what it does
not, one top-level evidence table, methodology and before/after summaries — see
[reports/performance_evidence_summary_2026_06_01.md](reports/performance_evidence_summary_2026_06_01.md).
It transcribes existing measured results only; it adds no new benchmark numbers.

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

## Serious Linux Performance Evaluation

`scripts/run_perf_evaluation.py` runs a larger-corpus standard-vs-pooled L3 hot-path evaluation.
It generates a deterministic corpus matrix (100k baseline plus 1M balanced, high-cancellation,
replace-heavy, deep-book, bursty, wide-price-range and an optional multi-symbol-style corpus) under
the git-ignored `build/perf_corpora/`, runs `asterion_benchmarks --only-hot-path` (which emits both
the standard `hot_path_binary_replay_l3_l2_strategy_risk` row and the pooled
`hot_path_binary_replay_pooled_l3_l2_strategy_risk` row in one pass), and writes a per-corpus
comparison plus a `corpus_manifest.json` (mode, seed, event count, symbol count, SHA-256, exact
generation command, format) under the git-ignored `build/perf_results/`. The multi-symbol-style
corpus is generated and checksummed for reviewer visibility, but is skipped by the hot-path
benchmark because that benchmark is deliberately single-symbol.

```bash
cmake --build build --target asterion_benchmarks
python scripts/run_perf_evaluation.py --warmup 5 --measure 30      # full 1M matrix
python scripts/run_perf_evaluation.py --datasets baseline_100k --events-cap 100000  # quick
python scripts/run_perf_evaluation.py --list
```

For each corpus it records event count, warm-up/measured iterations, p50/p95/p99/p99.9/max,
throughput, allocation count and bytes, the guard checksum, standard-vs-pooled checksum parity and
whether the pooled path stayed allocation-free in steady state. Large corpora are never committed.

On Linux, `scripts/run_linux_perf_profile.sh` profiles the hot path with `perf stat -d`
(cycles/instructions/branches/branch-misses/cache-references/cache-misses/context-switches/
page-faults) and optional `perf record` + flamegraph, writing text output under the git-ignored
`build/perf_profile/`. It fails loudly (it never fakes results) when `perf` is unavailable. The
manual `linux-performance` GitHub workflow runs the same path on dispatch only; it is non-blocking,
gates on no numbers, and notes that GitHub-hosted runners often do not expose hardware counters.

Three curated Linux/perf-adjacent evaluation reports are checked in. The Windows/MSYS2 standard-vs-pooled
evaluation is
[reports/linux_performance_evaluation_2026_05_31.md](reports/linux_performance_evaluation_2026_05_31.md).
The **WSL2 Linux** pass — `perf stat -d` hardware counters (cycles/IPC/branch/cache),
`perf record` hotspots, a 1M standard-vs-pooled hot path, 1M SPSC steady-state and a
LinearModel inference replay-loop comparison — is
[reports/linux_performance_evaluation_2026_06_01.md](reports/linux_performance_evaluation_2026_06_01.md).
The **Durham Hamilton8 HPC** pass - GCC Release build/test on a Slurm compute node,
1M standard-vs-pooled hot path, six 1M SPSC steady-state rows, LinearModel
replay-loop inference, explicit `perf stat` counters and completed hotspots for two
targets - is
[reports/durham_hpc_performance_evaluation_2026_06_04.md](reports/durham_hpc_performance_evaluation_2026_06_04.md).
All are representative measurements on the stated machine/environment, not portable
performance claims; the WSL2 run uses a virtualized PMU and the Durham run is one
shared allocation with no `LLC` events and no root governor/turbo control. Per-run
`Full`-validation replay is O(book/event) and does not scale to 1M, so the
large-corpus Linux rows use `--only-steady-state-replay` and
`--steady-state-validation-mode light` (a throughput mode, not a correctness
substitute). See also [docs/profiling.md](docs/profiling.md).

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
