# Asterion

**Asterion: Deterministic Low-Latency Trading Systems Lab**

CV title: **C++20 Low-Latency Trading Engine with Market Replay, Risk Gateway and Real-Time ML Inference**

Asterion is a Linux-first C++20 trading systems lab focused on deterministic replay, L3 order book reconstruction, price-time-priority matching, pre-trade risk checks, execution reports, latency instrumentation and correctness testing. It is intentionally built as a serious foundation rather than a toy exchange simulator.

It does **not** claim to be a real exchange, a live trading system, or a true production HFT stack. The goal is to make the important engineering properties visible: deterministic behavior, testability, clean boundaries and benchmarkability.

## What Asterion Proves

- Integer tick prices in the hot path; no floating-point prices in matching or book state.
- L3 book reconstruction with order-ID lookup, FIFO queues per price level and deterministic checksums.
- Recorded/simulated market-data logs in CSV and a compact ITCH-like binary format.
- Thin Python bindings and analysis helpers for log conversion, replay and checksum inspection.
- Price-time-priority matching for limit, market, cancel and replace flows.
- Structured execution reports with deterministic report checksums.
- Pre-trade risk gateway with quantity, notional, position, exposure, price-band, stale-data,
  duplicate-ID and kill-switch checks, plus opt-in open-order (working) exposure, per-client
  fixed/sliding-window message-rate limiting, self-trade prevention, replace-order rechecks,
  simulated cancel-on-kill/cancel-on-disconnect exposure release and persistent audit logging.
- Golden trace tests and randomized invariant tests.
- Measured inference infrastructure through `Model`, `LinearModel`, `FeatureExtractor`,
  timeout/late-signal policy hooks and a documented TorchScript-style placeholder interface.

## Architecture

```text
CSV / binary / synthetic events
        |
        v
 Market replay + diagnostics
        |
        v
   L3 order book  ---> L2 view ---> strategies / feature extraction / model score
        ^
        |
 Risk gateway ---> matching engine ---> execution reports ---> report checksum
        |
        v
  telemetry + benchmark runner
```

## Implemented Features

- C++20 CMake project with Ninja-compatible builds.
- Core types for timestamps, tick prices, quantities, symbols and order IDs.
- Fixed market-data event schema: Add, Cancel, Replace, Execute, Trade, Snapshot and Heartbeat.
- Snapshot event loading that resets and reconstructs the L3 book from framed Snapshot records while
  preserving deterministic checksums.
- ITCH-like binary event logs with safe malformed/truncated input rejection.
- Correctness-first L3 book using `std::unordered_map`, `std::map` and FIFO lists.
- Book invariant checks and deterministic final checksums.
- Matching engine with partial fills, full fills and resting-price execution.
- Execution report schema with status, execution type, fill fields and reject reason.
- Risk gateway and kill switch.
- Deterministic CSV and binary replay, sample replay data and replay diagnostics.
- Aggregate per-symbol replay summaries over multi-symbol logs. The default path is the stable
  grouped single-symbol replay view; an opt-in shared `MultiSymbolBookSet` replay path is also
  tested for parity on deterministic generated streams.
- Simulated market-data adapter modes for balanced, bursty, deep-book, high-cancel,
  wide-range and multi-symbol streams.
- Python bindings and a small `python/asterion` package for event logs, replay diagnostics,
  checksums, aggregate summaries and benchmark JSON summaries.
- Catch2 tests for unit, golden and randomized property-style coverage.
- Chrono benchmark executable with stable JSON output and allocation counters.
- Optional Google Benchmark target behind an explicit CMake flag.
- Strategy interface with market-maker and imbalance examples.
- Deterministic linear inference backend, measured inference latency accounting and a
  TorchScript-style placeholder that documents the future external-model boundary.
- Explicit inference backend selection (`make_inference_backend`) with an optional ONNX Runtime
  backend behind a CMake flag that deterministically falls back to `LinearModel` when absent.
- Optional shared multi-symbol replay (`replay_shared_by_symbol`) that routes an interleaved stream
  through `MultiSymbolBookSet`, emits per-symbol diagnostics/summaries and reports a combined book
  checksum. It is not a cross-symbol matching engine and grouped replay remains the default.
- Local historical benchmark store and cross-run trend reporting that reuse the regression schema.
- Configurable per-stage latency-budget accounting (replay, book update, matching, risk,
  strategy, inference and total) with budget-used/exceeded reporting, worst-offender
  detection and stable JSON output.
- Pre-trade risk audit trail recording accepted/rejected decisions, automatic working-exposure
  release from execution reports, optional append-only text/JSONL audit logs and a deterministic
  audit checksum, with opt-in file rotation and verification tooling.
- Offline benchmark regression comparison and a replay/benchmark inspection CLI with readable
  text and JSON output.
- GitHub Actions CI for Linux build and test.

## Build

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

Debug build with sanitizers:

```bash
cmake -S . -B build-debug -G Ninja -DCMAKE_BUILD_TYPE=Debug -DASTERION_ENABLE_SANITIZERS=ON
cmake --build build-debug
```

Catch2 v3 is used for tests. CMake will use a system package if available or fetch Catch2 during configure.

Python bindings are optional:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_PYTHON=ON
cmake --build build
PYTHONPATH=build/python python -m pytest python/tests
```

## Test

```bash
ctest --test-dir build --output-on-failure
```

Convenience script:

```bash
./scripts/run_tests.sh
```

## Replay And Event Logs

The canonical event fields are:

```text
timestamp_ns,sequence_number,symbol_id,event_type,side,price_ticks,quantity,order_id,trade_id,flags
```

CSV logs use that header. Binary logs start with `ASTITCH1`, version `1`, a 16-byte
header and fixed 58-byte little-endian records:

```text
int64 timestamp_ns
uint64 sequence_number
uint32 symbol_id
uint8 event_type
uint8 side
uint32 flags
int64 price_ticks
int64 quantity
uint64 order_id
uint64 trade_id
```

Replay auto-detects binary logs by magic bytes, or accepts an explicit format:

```bash
cmake --build build --target asterion_replay
./build/asterion_replay --input data/samples/sample_replay.csv
./build/asterion_replay --input data/samples/sample_replay.bin --format binary
python scripts/generate_synthetic_events.py --mode bursty --events 1000 --output data/generated/bursty.bin
PYTHONPATH=build/python python scripts/convert_event_log.py --input data/samples/sample_replay.csv --output data/generated/sample_replay.bin
```

Replay reports deterministic final-book, execution-report, event-log and diagnostics checksums.
Diagnostics include event index, sequence number, symbol, severity and reason for sequence gaps,
timestamp reversals, duplicate order IDs, unknown cancels/replaces, invalid prices or quantities,
invalid/crossed book states and malformed CSV/binary log diagnostics.

Snapshot events reconstruct book state. A snapshot block is framed in the shared event schema with
the `flags` field: the begin marker (`kSnapshotBeginFlag`) clears the book, each Snapshot record that
carries a valid `order_id` reinstates one resting order, and the end marker (`kSnapshotEndFlag`)
closes the block. A Snapshot record with `order_id == 0` is a pure begin/end marker. Reloaded orders
carry the snapshot record's timestamp and sequence number, so the resulting book checksum is
deterministic. Limitation: a snapshot is a sequence of single-order records, not an aggregated
L2-only image; levels without per-order detail cannot be represented.

Aggregate multi-symbol replay is available as a summary/helper view. By default it groups recorded
events by symbol, runs the existing single-symbol replay engine per group, and reports per-symbol
counts, first/last sequence numbers, diagnostics and checksum summaries. An opt-in shared path
(`--shared` in the CLI, `aggregate_by_symbol(..., shared=True)` in Python) routes the interleaved
stream through `MultiSymbolBookSet` in one pass and reports the same summary shape plus a combined
book checksum. Tests assert parity with grouped replay for deterministic generated streams. Neither
path implements cross-symbol matching.

Deterministic shared-replay fuzz summaries are exposed through Python and the inspection CLI. They
are parity fixtures for the opt-in shared path; grouped replay remains the default.

## Python Usage

```bash
PYTHONPATH=build/python python python/examples/replay_samples.py
PYTHONPATH=build/python python python/examples/compare_checksums.py
PYTHONPATH=build/python python python/examples/plot_diagnostics_counts.py
PYTHONPATH=build/python python python/examples/load_benchmark_json.py
```

```python
import asterion

events = asterion.load_log("data/samples/sample_replay.csv")
result = asterion.run_replay(events, symbol_id=1)
summary = asterion.aggregate_by_symbol(events)
shared = asterion.aggregate_by_symbol(events, shared=True)
print(result.final_book_checksum, summary.symbol_count)
```

## Benchmark

Benchmarks are generated locally. No benchmark numbers are checked into this repository.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asterion_benchmarks
./build/asterion_benchmarks
./build/asterion_benchmarks --json build/asterion_benchmark.json --no-text
```

The runner measures add, cancel, replace, market crossing, L2 snapshot, replay, risk-check,
linear-inference and measured-linear-inference paths using `std::chrono`. Google Benchmark
integration is optional:

```bash
cmake -S . -B build-gbench -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_GOOGLE_BENCHMARK=ON
cmake --build build-gbench --target asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks --benchmark_format=json
```

No benchmark results are checked into this repository because they are hardware-dependent.
See [BENCHMARKS.md](BENCHMARKS.md) and [docs/profiling.md](docs/profiling.md) for commands.

## Latency Budget

`asterion_latency_budget` times each stage of the tick-to-trade path (replay, book update,
matching, risk, strategy, inference and a combined total) and reports observed worst-case and
total nanoseconds against configurable budgets. Budgets default to `0`, which means "no budget
configured": the stage is still measured but never flagged as exceeded. There are no hard-coded
latency targets because realistic budgets depend on hardware and workload.

```bash
cmake --build build --target asterion_latency_budget
./build/asterion_latency_budget --iterations 1000
./build/asterion_latency_budget --risk-budget-ns 500 --json build/latency_budget.json --no-text
python scripts/asterion_inspect.py latency-budget --input build/latency_budget.json --json
```

Observed latencies are machine-dependent. The configuration checksum is deterministic; the
measured nanoseconds are not.

## Inspection CLI

`scripts/asterion_inspect.py` inspects replay output and benchmark/latency JSON in readable text
(default) or `--json`:

```bash
# Replay inspection (requires the built Python bindings on PYTHONPATH).
PYTHONPATH=build/python python scripts/asterion_inspect.py replay-checksums --input data/samples/sample_replay.csv --json
PYTHONPATH=build/python python scripts/asterion_inspect.py diagnostics --input data/samples/sample_replay.csv
PYTHONPATH=build/python python scripts/asterion_inspect.py per-symbol --input data/samples/sample_replay.csv --json
PYTHONPATH=build/python python scripts/asterion_inspect.py per-symbol --input data/samples/sample_replay.csv --shared --json
PYTHONPATH=build/python python scripts/asterion_inspect.py shared-fuzz --json
PYTHONPATH=build/python python scripts/asterion_inspect.py risk-exposure --input data/samples/sample_risk_flow.json --json
PYTHONPATH=build/python python scripts/asterion_inspect.py risk-exposure --input data/samples/sample_disconnect_replace_risk.json --json

# Offline JSON inspection (no compiled extension required).
python scripts/asterion_inspect.py benchmark-summary --input data/samples/sample_benchmark_baseline.json
python scripts/asterion_inspect.py latency-budget --input data/samples/sample_latency_budget.json --json
python scripts/asterion_inspect.py audit-summary --input data/samples/sample_risk_audit.jsonl --json
python scripts/asterion_inspect.py audit-verify --input data/samples/sample_risk_audit.jsonl --json
python scripts/asterion_inspect.py rate-limit-mode --mode sliding-window --json
```

## Benchmark Regression Comparison

The offline comparison tool reports percentage changes, new/missing benchmarks and configurable
threshold breaches between two benchmark JSON files:

```bash
python scripts/asterion_inspect.py benchmark-compare \
  --baseline data/samples/sample_benchmark_baseline.json \
  --current data/samples/sample_benchmark_current.json \
  --threshold-pct 10 --json
```

It exits non-zero only with `--fail-on-regression`, so normal runs never fail on performance
variance. Regression results are machine-dependent and are only meaningful when both JSON files
were produced on the same controlled hardware. The `sample_benchmark_*.json` files are synthetic
tooling fixtures, not measurements.

## Benchmark History And Trends

Benchmark JSON files can be stored locally and compared over time. History lives under
`benchmarks/history/`, which is git-ignored, so no numbers are committed. Trend reporting reuses the
regression schema and is kept out of CI performance gates.

```bash
python scripts/asterion_inspect.py benchmark-store --input build/asterion_benchmark.json --history-dir benchmarks/history
python scripts/asterion_inspect.py benchmark-trend --history-dir benchmarks/history --metric avg_ns --json
```

Trends are only meaningful when every stored run was produced on the same controlled hardware under
comparable conditions; cross-machine trends are not meaningful.

## Optional ONNX Runtime Backend

The inference layer selects a backend through `make_inference_backend`. The default and fallback is
the deterministic `LinearModel`. An optional ONNX Runtime backend is available behind a CMake flag;
when the dependency is absent the build still succeeds and ONNX requests fall back to `LinearModel`.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_ONNXRUNTIME=ON
```

The ONNX backend is optional and is not exercised by default CI; it requires ONNX Runtime to be
installed for a real ONNX backend to load. Default CI remains dependency-free. A tiny checked-in
identity-model fixture is decoded only by tests compiled with `ASTERION_HAVE_ONNXRUNTIME`; otherwise
the fallback path is tested. The `ci` workflow has a manual `onnx_backend` input that configures with
`-DASTERION_USE_ONNXRUNTIME=ON` and verifies the deterministic backend-selection lane.

## Risk Audit Trail

The risk gateway can record every accepted or rejected order in an audit trail, capturing timestamp,
client order ID, symbol, deciding check name, decision, reject reason and the relevant limit and
observed values. The trail exposes a deterministic checksum for reproducible comparison across runs.
Recording is opt-in (`set_audit_enabled(true)` or `open_audit_log(...)`) so the pre-trade hot path
stays allocation-free by default. Persistent audit logs are append-only text or JSONL and include
the deterministic audit checksum; they do not add a wall-clock timestamp to that checksum. Rotation
by record count or byte size and checksum verification across rotated files are opt-in. See
[RISK.md](RISK.md).

## Honesty And Limitations

Asterion is a deterministic systems lab. The market-data ingestion path is for recorded and
simulated logs only. It is not connected to any exchange, does not trade live, does not implement
kernel bypass, does not claim true HFT production performance and does not include fabricated
benchmark results. The inference path measures model plumbing and policy accounting only; it is
not a profitable strategy claim. See [LIMITATIONS.md](LIMITATIONS.md) for the full scope statement.

## Example CV Bullet

Built **Asterion**, a C++20 deterministic trading systems lab implementing L3 order book reconstruction, price-time-priority matching, pre-trade risk checks, market replay, execution-report checksums, correctness tests and latency benchmark scaffolding.
