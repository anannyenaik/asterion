# Asterion

[![ci](https://github.com/anannyenaik/asterion/actions/workflows/ci.yml/badge.svg)](https://github.com/anannyenaik/asterion/actions/workflows/ci.yml)
[![sanitizers](https://github.com/anannyenaik/asterion/actions/workflows/sanitizers.yml/badge.svg)](https://github.com/anannyenaik/asterion/actions/workflows/sanitizers.yml)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white)](https://en.cppreference.com/w/cpp/20)
[![Python](https://img.shields.io/badge/Python-3.x-3776AB?logo=python&logoColor=white)](python/)
[![platform](https://img.shields.io/badge/platform-Linux--first-555555)](#build)

The default, dependency-light checks are the `ci` workflow (GCC/Clang Release
builds, Python bindings and demo smoke tests) and the `sanitizers` workflow
(ASan/UBSan C++ tests). They exclude optional ONNX Runtime and all benchmark
workflows, and never gate on performance numbers.

**Asterion: Deterministic Low-Latency Trading Systems Lab**

CV title: **C++20 Deterministic Trading Systems Lab with Market Replay, Risk Gateway and Measured Inference**

Asterion is a Linux-first C++20 trading systems lab focused on deterministic replay, L3 order book reconstruction, price-time-priority matching, pre-trade risk checks, execution reports, latency instrumentation and correctness testing. It is intentionally built as a serious foundation rather than a toy exchange simulator.

It does **not** claim to be a real exchange, a live trading system, or a true production HFT stack. The goal is to make the important engineering properties visible: deterministic behavior, testability, clean boundaries and benchmarkability.

## Representative Benchmark Evidence

These are representative measurements under the disclosed source-report conditions,
not portable latency or production-HFT claims. Durham Hamilton8 HPC is the primary
performance context; the isolated public-L2 ONNX row is retained as local
Windows/MSYS2 systems-cost evidence.

| Evidence | Environment | Compiler/build | Dataset/workload | p50 | p95 | p99 | p99.9 | Throughput | Allocations after warm-up | Determinism/checksum | Source |
| --- | --- | --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | --- | --- |
| Standard L3 hot path | Durham Hamilton8 HPC, Rocky Linux | GCC 13.2, Release `-O3` | high-cancellation, 1M events x 5 | 281 ns | 501 ns | 631 ns | 812 ns | 2,879,594 ev/s | 7,340,580 total | guard `14180005740461440914` | [Durham HPC report](reports/durham_hpc_performance_evaluation_2026_06_04.md#high-cancellation-1m-hot-path) |
| Opt-in pooled L3 hot path | Durham Hamilton8 HPC, Rocky Linux | GCC 13.2, Release `-O3` | high-cancellation, 1M events x 5 | 291 ns | 491 ns | 681 ns | 832 ns | 2,815,909 ev/s | **0 total** | same guard as standard path | [Durham HPC report](reports/durham_hpc_performance_evaluation_2026_06_04.md#high-cancellation-1m-hot-path) |
| Opt-in SPSC steady-state replay | Durham Hamilton8 HPC, Rocky Linux | GCC 13.2, Release `-O3` | high-cancellation, 1M events, `Light` validation | not measured | not measured | not measured | not measured | 4,627,770 ev/s | 1,468,122 total | parity true; dropped 0 | [Durham HPC report](reports/durham_hpc_performance_evaluation_2026_06_04.md#1m-steady-state-spsc-replay) |
| LinearModel replay-loop inference | Durham Hamilton8 HPC, Rocky Linux | GCC 13.2, Release `-O3` | balanced 10k, 500k measured events | 511 ns | 671 ns | 792 ns | 1,002 ns | 1,649,716 ev/s | 703,450 total; inference adds **0** | guard `1442857765714779360` | [Durham HPC report](reports/durham_hpc_performance_evaluation_2026_06_04.md#balanced-10k-inference-replay-loop) |
| Isolated public-L2 ChronosLOB ONNX inference | Windows 10 / MSYS2 UCRT64, local | GCC 16.1, Release, ONNX Runtime 1.20.1 | recorded-public-L2 `[1,16,40]` contract, 20k calls | 36.9 us | 72.3 us | 108.9 us | 409.7 us | about 23.2k inf/s | 2/call; 40,000 total | expected output reproduced within `1e-3` | [Public-L2 model-bridge report](reports/chronoslob_public_l2_model_bridge_report_2026_06_04.md) |

See [BENCHMARKS.md](BENCHMARKS.md), the
[performance deep dive](docs/performance_deep_dive.md) (baseline → hotspot →
opt-in pooled book → before/after, as a measured case study) and the
[performance evidence summary](reports/performance_evidence_summary_2026_06_01.md)
for methodology, the optimisation narrative, additional rows and limitations.
Windows/MSYS2 and WSL2 results remain historical/local development baselines, and
ONNX Runtime remains optional.

## How To Review This Repo In 10 Minutes

```bash
./scripts/configure_release.sh
cmake --build build
ctest --test-dir build --output-on-failure
PYTHONPATH=build/python python -m pytest python/tests
./scripts/run_demo.sh --skip-build
```

Windows PowerShell equivalents are available for the same path:

```powershell
.\scripts\configure_release.ps1
cmake --build build
ctest --test-dir build --output-on-failure
$env:PYTHONPATH = "$PWD\build\python"
python -m pytest python/tests
.\scripts\run_demo.ps1 -SkipBuild
```

Windows PowerShell helpers fall back to an existing MSYS2/MinGW-w64 toolchain
(`C:\msys64\ucrt64\bin`) when `cmake` is not already on `PATH`.

For a quick reviewer pass, read this README, then skim the supporting docs:
[architecture overview](docs/architecture_overview.md), [DESIGN.md](DESIGN.md),
[CORRECTNESS.md](CORRECTNESS.md), [RISK.md](RISK.md),
[docs/matching_semantics.md](docs/matching_semantics.md),
[BENCHMARKS.md](BENCHMARKS.md), [LIMITATIONS.md](LIMITATIONS.md),
[ROADMAP.md](ROADMAP.md) and [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md). The current
release candidate is described in [RELEASE_NOTES.md](RELEASE_NOTES.md). Then run the demo.
The demo uses checked-in sample data and writes generated audit/latency/benchmark
artifacts under `build/demo/`, which is ignored by git.

**What this 10-minute path proves:** the project builds clean in Release, the C++ and
Python test suites pass, and the demo reproduces deterministic checksums (book, execution
report, diagnostics, audit chain, latency config) on checked-in data — while machine-dependent
timings vary. **What it does not claim:** any live connectivity, production HFT performance or
portable benchmark guarantees (see [What This Does Not Claim](#what-this-does-not-claim)).

**Reviewer shortcuts** — three docs make the audit fast:

- [docs/claim_audit.md](docs/claim_audit.md) — every major claim classified against its evidence.
- [docs/evidence_index.md](docs/evidence_index.md) — "where is X tested?" → file + command.
- [reports/README.md](reports/README.md) — every report with scope, optional deps and limitation.

> Toolchain note (Windows): the compiled Python extension is ABI-specific to the Python
> that built it. Build and run pytest/the demo with the **same** interpreter (pass
> `-PythonExe` to the PowerShell scripts to pin it). Default Linux CI uses one interpreter
> throughout. See [docs/evidence_index.md](docs/evidence_index.md#toolchain-note-windows).

## What This Proves

- Integer tick prices in the hot path; no floating-point prices in matching or book state.
- L3 book reconstruction with order-ID lookup, FIFO queues per price level and deterministic checksums.
- Recorded/simulated market-data logs in CSV and a compact ITCH-like binary format.
- Thin Python bindings and analysis helpers for log conversion, replay and checksum inspection.
- Price-time-priority matching for limit, market, cancel and replace flows, with explicit
  IOC, FOK, post-only and deterministic self-trade-prevention semantics, cross-checked
  against an independent Python reference matcher on golden and fixed-seed random flows
  ([docs/reference_matcher.md](docs/reference_matcher.md)).
- Structured execution reports with deterministic report checksums.
- Pre-trade risk gateway with quantity, notional, position, exposure, price-band, stale-data,
  duplicate-ID and kill-switch checks, plus opt-in open-order (working) exposure, per-client
  fixed/sliding-window message-rate limiting, self-trade prevention, replace-order rechecks,
  simulated cancel-on-kill/cancel-on-disconnect exposure release and persistent audit logging.
- Golden trace tests and randomized invariant tests.
- Measured inference infrastructure through `Model`, `LinearModel`, `FeatureExtractor`,
  timeout/late-signal policy hooks and a documented TorchScript-style placeholder interface.
- A reproducible reviewer demo that exercises replay, diagnostics, parity, audit manifests,
  simulated risk snapshots, latency-budget JSON and benchmark JSON generation without external
  services or committed benchmark results.

## What This Does Not Claim

- No live exchange, broker or market-data connectivity.
- No production HFT performance, kernel-bypass, FPGA, colocated networking or profitability claim.
- No portable benchmark or latency numbers; generated timing JSON and curated reports are local
  machine evidence only.
- No managed audit retention, custody, compliance guarantee or tamper-proof storage.
- No full portfolio management, market-risk feed or cross-symbol matching engine.

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

See [docs/architecture_overview.md](docs/architecture_overview.md) for the
one-page pipeline, optional side paths and explicit non-claims.

## Key Modules

| Area | Where | Tests |
| --- | --- | --- |
| Event log + replay (CSV/binary, diagnostics, checksums) | `cpp/src/market_data/`, `cpp/include/asterion/market_data/` | `tests/unit/test_event_log_replay.cpp`, `test_snapshot.cpp` |
| L3 order book + matching | `cpp/src/book/`, `cpp/src/matching/` | `tests/unit/test_order_book.cpp`, `test_matching_semantics.cpp` |
| Pre-trade risk + audit | `cpp/src/risk/` | `tests/unit/test_risk_gateway.cpp`, `test_risk_controls.cpp`, `test_risk_audit.cpp`, `test_audit_manifest.cpp` |
| Inference plumbing + ONNX bridge | `cpp/src/inference/` | `tests/unit/test_inference_backend.cpp`, `test_telemetry_inference.cpp` |
| Telemetry / latency budget | `cpp/src/telemetry/` | `tests/unit/test_latency_budget.cpp` |
| SPSC replay pipeline (opt-in) | `cpp/src/market_data/spsc_replay.cpp` | `tests/unit/test_spsc_replay.cpp`, `test_spsc_ring_buffer.cpp` |
| Pooled order book (opt-in) | `cpp/src/book/` | `tests/unit/test_pooled_order_book.cpp` |
| Python bindings + tooling | `python/asterion/`, `scripts/asterion_inspect.py` | `python/tests/` |

See [docs/claim_audit.md](docs/claim_audit.md) for the full claim→evidence map and
[reports/README.md](reports/README.md) for the report-by-report scope table.

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
- Explicit IOC/FOK/post-only behavior and documented reject-vs-cancel/order-state transitions.
- Execution report schema with status, execution type, fill fields and reject reason.
- Risk gateway and kill switch.
- Deterministic CSV and binary replay, sample replay data and replay diagnostics.
- One opt-in concurrency boundary: a bounded single-producer/single-consumer (SPSC) replay pipeline
  (`run_spsc_replay`) whose consumer reuses the single-thread `ReplayEngine` path for bit-identical
  checksums, with lossless-blocking backpressure by default and an opt-in drop policy. Deterministic
  single-thread replay remains the default; this is not production networking or live connectivity.
- Opt-in steady-state SPSC replay evaluation (`run_spsc_replay_steady_state`) and
  `ReplayValidationMode.Light` for large-corpus throughput measurements with controlled validation
  cost. Full validation remains the default correctness path.
- Aggregate per-symbol replay summaries over multi-symbol logs. The default path is the stable
  grouped single-symbol replay view; an opt-in shared `MultiSymbolBookSet` replay path is also
  tested for parity on deterministic generated streams.
- Simulated market-data adapter modes for balanced, bursty, deep-book, high-cancel,
  wide-range and multi-symbol streams.
- Python bindings and a small `python/asterion` package for event logs, replay diagnostics,
  checksums, aggregate summaries and benchmark JSON summaries.
- Catch2 tests for unit, golden and randomized property-style coverage.
- Chrono benchmark executable with stable JSON output, allocation counters and a measured hot-path
  benchmark for binary replay -> L3 update -> reusable L2 view -> strategy callback -> risk check.
- Optional Google Benchmark target behind an explicit CMake flag.
- Strategy interface with market-maker and imbalance examples.
- Deterministic linear inference backend, measured inference latency accounting and a
  TorchScript-style placeholder that documents the future external-model boundary.
- Explicit inference backend selection (`make_inference_backend`) with an optional ONNX Runtime
  backend behind a CMake flag that deterministically falls back to `LinearModel` when absent.
- Optional shared multi-symbol replay (`replay_shared_by_symbol`) that routes an interleaved stream
  through `MultiSymbolBookSet`, emits per-symbol diagnostics/summaries and reports a combined book
  checksum. It is not a cross-symbol matching engine and grouped replay remains the default.
- Structured grouped-vs-shared replay parity reports for the opt-in shared replay path.
- Local historical benchmark store and cross-run trend reporting that reuse the regression schema.
- A checked-in representative benchmark report for one optimized local hot path, clearly labelled as
  non-portable local evidence: [reports/benchmark_report_2026_05_31.md](reports/benchmark_report_2026_05_31.md).
- Configurable per-stage latency-budget accounting (replay, book update, matching, risk,
  strategy, inference and total) with budget-used/exceeded reporting, worst-offender
  detection and stable JSON output.
- Pre-trade risk audit trail recording accepted/rejected decisions, automatic working-exposure
  release from execution reports, optional append-only text/JSONL audit logs and a deterministic
  audit checksum, with opt-in file rotation and verification tooling.
- Tamper-evident audit manifests over audit logs with optional HMAC-SHA256 signing. Signing is
  opt-in, local-key based and not a managed retention or compliance system.
- Simulated broker/session lifecycle state machine for connect, disconnect, reconnect, pending
  cancels and fills. It never sends live broker or exchange messages.
- Simulated portfolio risk monitor for caller-supplied marks, exposure, concentration and PnL
  thresholds. It is an opt-in accounting gate, not a live portfolio management system.
- Offline benchmark regression comparison and a replay/benchmark inspection CLI with readable
  text and JSON output.
- One-command demo scripts for Linux/macOS shell and Windows PowerShell that run only on checked-in
  sample data and ignored generated outputs.
- GitHub Actions CI with reviewer-readable jobs: `gcc-release`, `clang-release`,
  `python-bindings` (bindings, pytest, inspection-CLI and one-command demo smoke
  tests) and default-gating `asan-ubsan`, plus manual fuzz/ONNX/benchmark workflows.
  The default checks are dependency-light and never gate on performance numbers. See
  [Continuous Integration](#continuous-integration).

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

Opt-in Clang/libFuzzer robustness targets are documented in
[FUZZING.md](FUZZING.md). They are disabled by default and do not affect the
normal build or default CI.

Catch2 v3 is used for tests. CMake will use a system package if available or fetch Catch2 during configure.

Python bindings are optional:

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_PYTHON=ON
cmake --build build
PYTHONPATH=build/python python -m pytest python/tests
```

For an Ubuntu-based Docker/devcontainer path with Release, Python, demo and
sanitizer commands, see
[docs/reproducible_dev_environment.md](docs/reproducible_dev_environment.md).
ONNX Runtime and heavy benchmarks are deliberately not installed or run by default.

## Test

```bash
ctest --test-dir build --output-on-failure
```

Convenience script:

```bash
./scripts/run_tests.sh
```

## Continuous Integration

CI is structured for **reviewer-visible correctness, portability and optional-inference
verification** — not benchmark gating or production validation. The default `ci`
workflow runs on every push and pull request to `main` and is intentionally
dependency-light (no ONNX Runtime, no large benchmarks, no network beyond normal
package setup). Jobs are named so a reviewer can read the check list directly:

| Job | Proves |
| --- | --- |
| `gcc-release` | C++20 Release build + `ctest` on GCC with strict warnings. |
| `clang-release` | The same build + tests on Clang, i.e. cross-compiler portability. |
| `python-bindings` | Python bindings build, `pytest`, the inspection CLI and the one-command demo all pass on a single interpreter. |
| `asan-ubsan` | Debug C++ unit/golden/property suite passes under Address + UndefinedBehaviour sanitizers. |

An Address/UndefinedBehaviour sanitizer lane (`asan-ubsan`) lives in a separate
default-gating workflow, [`sanitizers.yml`](.github/workflows/sanitizers.yml). It
builds the unit/golden/property suite in Debug with
`-DASTERION_ENABLE_SANITIZERS=ON`, keeps benchmarks and ONNX Runtime disabled,
and runs `ctest` on every push/PR without a raised stack limit. The metadata
loader uses bounded iterative scanning for numeric arrays, so the full committed
public-L2 `[1,16,40]` model-contract fixture remains covered. This is
correctness/memory-UB evidence, not benchmark, performance, predictive-quality
or production-readiness evidence.

Two ONNX lanes live in the same default workflow but only run on manual
`workflow_dispatch` with the `onnx_backend` input enabled, and are never part of
default CI:

- `onnx-fallback-manual` — configures with `-DASTERION_USE_ONNXRUNTIME=ON` while the
  dependency is **absent**, proving the build still succeeds and inference falls back
  to the deterministic `LinearModel`.
- `onnx-runtime-manual` — downloads a real ONNX Runtime, builds the ONNX backend and
  loads the checked-in ChronosLOB artefacts (hand-written fixture, real tiny trained
  model, and the recorded-public-L2 model-contract artefact). This is
  systems-integration / model-contract evidence only — **no predictive-quality,
  profitability, live-trading or production-serving claim**.

Three further workflows are manual-only and never gate `main`: `fuzz-smoke`
(bounded Clang/libFuzzer + ASan/UBSan robustness smoke tests), `benchmarks`
(emits benchmark JSON; comparisons are informational, with no `--fail-on-regression`)
and `linux-performance` (best-effort `perf` profiling, honestly recording when a
hosted runner exposes no hardware counters). **Benchmark numbers are reported, not
CI-gated.** The primary performance context is the Durham HPC evidence under
[reports/](reports/), not hosted-runner timings. See
[docs/evidence_index.md](docs/evidence_index.md) and
[docs/claim_audit.md](docs/claim_audit.md) for what each lane does and does not prove.

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

The schema boundary is recorded in
[`data/schema/event_log_schema_v1.json`](data/schema/event_log_schema_v1.json) and documented in
[`docs/event_log_schema.md`](docs/event_log_schema.md). Binary layout, CSV column order, enum wire
values and fixture checksum drift tests point there when an intentional migration is required.

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

`compare_replay_parity(...)` returns a structured grouped-vs-shared parity report with per-symbol
checksum agreement, combined-book agreement and aggregate checksum agreement, and
`describe_replay_parity(...)` renders any mismatch for reproduction. Deterministic shared-replay fuzz
summaries are exposed through Python and the inspection CLI. They are parity fixtures for the opt-in
shared path; grouped replay remains the default unless shared replay parity is exhaustively validated
and documented. The parity contract, hand-written/fixed-seed fixtures and diagnostic-index
normalisation are in [docs/shared_replay_parity.md](docs/shared_replay_parity.md); parity coverage is
stronger for tested cases, not exhaustively proven for all workloads.

## Recorded Binance Public Depth Case Study

A recorded **public** Binance order-book depth stream, normalised into Asterion's
event schema and replayed deterministically through the existing
replay/diagnostics pipeline.

**This is a recorded public market-data engineering demo. It is not live trading, not authenticated exchange connectivity, and not evidence of equities-market realism.** No API keys, no order placement, no broker connectivity, no profitability claim.

A tiny hand-curated fixture is checked in for deterministic CI and guarded by
`data/samples/binance_depth_sample.expected.json`; local capture is opt-in and
manual (never run in CI). Reviewer path:

```bash
# Normalise the checked-in fixture into Asterion CSV + binary event logs.
python tools/normalise_binance_depth_to_asterion.py \
  --input data/samples/binance_depth_sample.raw.jsonl \
  --csv-output build/binance_sample.csv \
  --binary-output build/binance_sample.bin --json

# Replay the normalised binary through the existing replay engine.
./build/asterion_replay --input build/binance_sample.bin --format binary

# Replay summary / checksums via the existing inspection CLI.
PYTHONPATH=build/python python scripts/asterion_inspect.py \
  replay-checksums --input build/binance_sample.bin --format binary --json
```

Optional manual capture (public REST `/api/v3/depth`, no keys):

```bash
python tools/capture_binance_depth.py --symbol BTCUSDT --duration 20 \
  --max-events 40 --output data/captures/btcusdt_depth.raw.jsonl
```

Binance depth is L2 price-level data; Asterion's schema is L3/order-oriented. The
normaliser is an honest adapter that uses level-replacement semantics with
deterministic synthetic order IDs and does not fabricate real exchange order IDs.
The fixture-based tests re-run normalisation, compare regenerated CSV output,
check binary semantic properties and replay checksums, and assert CSV/binary
event tuple equivalence without network access.
See [docs/market_data.md](docs/market_data.md) and the case-study report
[reports/binance_replay_case_study_2026_05_31.md](reports/binance_replay_case_study_2026_05_31.md).
A larger compact recorded public L2 snapshot fixture and checksum/parity report are in
[reports/binance_larger_replay_case_study_2026_06_01.md](reports/binance_larger_replay_case_study_2026_06_01.md).

## Opt-In Concurrent Replay Pipeline (SPSC)

Asterion has exactly one explicit concurrency boundary: an **opt-in bounded
single-producer/single-consumer (SPSC) replay pipeline** for systems evaluation. A producer thread
publishes preloaded events, in order, into a fixed-capacity SPSC ring buffer; a consumer thread drains
them, in order, and runs the **same** `ReplayEngine` path, so the book / execution-report / diagnostics
checksums are bit-identical to the single-thread path regardless of thread timing.

**Deterministic single-thread replay remains the default. This is not production networking, not live
exchange connectivity, not production-HFT infrastructure and not a latency guarantee.** The default
backpressure policy is lossless blocking (the producer waits when the queue is full; nothing is
dropped). An opt-in `DropNewestOnFull` policy exists for overload-shedding experiments only and is not
correctness-preserving for order-book streams.

```bash
# Reviewer-facing parity + stats demo (needs the built Python bindings on PYTHONPATH).
PYTHONPATH=build/python python scripts/run_spsc_replay_demo.py \
  --input data/samples/sample_replay.csv --queue-capacity 4 --json

# Benchmark rows: single-thread baseline + SPSC pipeline (checksum parity reported).
./build/asterion_benchmarks --only-hot-path --hot-path-iterations 2000

# Large-corpus steady-state replay rows only; generated corpora should stay under ignored paths.
./build/asterion_benchmarks --only-steady-state-replay \
  --dataset data/generated/balanced_100k.bin \
  --steady-state-validation-mode light --spsc-queue-capacity 4096
```

```python
import asterion

events = asterion.load_log("data/samples/sample_replay.csv")
config = asterion.SpscReplayConfig()
config.queue_capacity = 8
result = asterion.run_spsc_replay(events, events[0].symbol_id, config)
print(result.replay.final_book_checksum, result.stats.produced_events, result.stats.dropped_events)

steady_config = asterion.SpscReplayConfig()
steady_config.replay.validation_mode = asterion.ReplayValidationMode.Light
steady = asterion.run_spsc_replay_steady_state(events, events[0].symbol_id, steady_config)
print(steady.stats.throughput_events_per_second, steady.stats.end_of_stream_markers_consumed)
```

See [reports/spsc_replay_pipeline_report_2026_05_31.md](reports/spsc_replay_pipeline_report_2026_05_31.md)
[reports/spsc_steady_state_report_2026_05_31.md](reports/spsc_steady_state_report_2026_05_31.md)
and the SPSC section of [DESIGN.md](DESIGN.md). These are representative local measurements, not
portable performance claims.

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

Benchmarks are generated locally. Generic machine-specific benchmark dumps stay out of git; the
curated report under `reports/` is explicitly labelled as local, non-portable evidence.

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build --target asterion_benchmarks
./build/asterion_benchmarks
./build/asterion_benchmarks --dataset data/samples/sample_hot_path_replay.bin --hot-path-iterations 10000
./build/asterion_benchmarks --json build/asterion_benchmark.json --no-text
```

The runner reports two categories. The `core` category measures the binary replay -> L3 update ->
reusable L2 view -> strategy callback -> risk check path plus add, cancel, replace, market crossing,
L2 snapshot, replay and risk-check paths. The `inference` category is measured separately and covers
feature extraction only, LinearModel inference only, feature extraction + LinearModel, the
measured-engine path, event-loop policy-gate overhead, and — only when built with ONNX Runtime — ONNX
inference only, feature extraction + ONNX, and the optional real ChronosLOB ONNX replay-loop row.
Inference rows use per-call timing so they report a real
p50/p95/p99/p99.9/max distribution along with backend, model name, input shape and allocation count.
The `inference` category also includes a full event-loop row
(`hot_path_binary_replay_l3_l2_inference_strategy_risk`) that inserts caller-owned feature extraction +
LinearModel + a measured policy gate into the replay hot path; on this machine it added 0 steady-state
allocations versus the inference-free hot path (plumbing only, no profitability claim) — see
[reports/inference_event_loop_cost_report_2026_06_01.md](reports/inference_event_loop_cost_report_2026_06_01.md).
Google Benchmark integration is optional:

```bash
cmake -S . -B build-gbench -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_USE_GOOGLE_BENCHMARK=ON
cmake --build build-gbench --target asterion_google_benchmarks
./build-gbench/asterion_google_benchmarks --benchmark_format=json
```

See [BENCHMARKS.md](BENCHMARKS.md), [docs/profiling.md](docs/profiling.md) and
[reports/benchmark_report_2026_05_31.md](reports/benchmark_report_2026_05_31.md) for methodology.

For a single cross-report view of the performance/allocation evidence — what it proves, what it does
not, one top-level evidence table and before/after summaries — see
[reports/performance_evidence_summary_2026_06_01.md](reports/performance_evidence_summary_2026_06_01.md)
(representative local measurements only; no new numbers).

The **primary curated performance evidence** is now the **Durham Hamilton8 HPC** Slurm compute-node
run (Rocky Linux, GCC Release, one shared allocation),
[reports/durham_hpc_performance_evaluation_2026_06_04.md](reports/durham_hpc_performance_evaluation_2026_06_04.md):
1M standard-vs-pooled hot path, six 1M SPSC steady-state rows, LinearModel replay-loop inference and
explicit Linux `perf` counters/hotspots. The older Windows/MSYS2 Lenovo-laptop run
([reports/linux_performance_evaluation_2026_05_31.md](reports/linux_performance_evaluation_2026_05_31.md))
and the WSL2-laptop `perf` run
([reports/linux_performance_evaluation_2026_06_01.md](reports/linux_performance_evaluation_2026_06_01.md))
are retained as **local development baselines**. To regenerate corpora and run the path yourself, use
`scripts/run_perf_evaluation.py` and `scripts/run_linux_perf_profile.sh`. None of these numbers are
portable or production-HFT claims; each is a representative measurement on its own host (WSL2 uses a
virtualized PMU; Durham is one shared allocation with no LLC events and no root governor/turbo
control), and cross-machine comparison is not meaningful.

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
PYTHONPATH=build/python python scripts/asterion_inspect.py replay-parity --input data/samples/sample_replay.csv --json
PYTHONPATH=build/python python scripts/asterion_inspect.py shared-fuzz --json
PYTHONPATH=build/python python scripts/asterion_inspect.py risk-exposure --input data/samples/sample_risk_flow.json --json
PYTHONPATH=build/python python scripts/asterion_inspect.py risk-exposure --input data/samples/sample_disconnect_replace_risk.json --json
PYTHONPATH=build/python python scripts/asterion_inspect.py portfolio-risk --input data/samples/sample_portfolio_risk.json --json
PYTHONPATH=build/python python scripts/asterion_inspect.py onnx-status --json
PYTHONPATH=build/python python scripts/asterion_inspect.py audit-manifest --input data/samples/sample_risk_audit.jsonl --output build/sample_risk_audit.manifest.jsonl --json
PYTHONPATH=build/python python scripts/asterion_inspect.py audit-manifest-verify --manifest build/sample_risk_audit.manifest.jsonl --base-dir data/samples --json

# Offline JSON inspection (no compiled extension required).
python scripts/asterion_inspect.py benchmark-summary --input data/samples/sample_benchmark_baseline.json
python scripts/asterion_inspect.py latency-budget --input data/samples/sample_latency_budget.json --json
python scripts/asterion_inspect.py audit-summary --input data/samples/sample_risk_audit.jsonl --json
python scripts/asterion_inspect.py audit-verify --input data/samples/sample_risk_audit.jsonl --json
python scripts/asterion_inspect.py rate-limit-mode --mode sliding-window --json
```

## Developer Scripts

```bash
./scripts/configure_release.sh
./scripts/configure_sanitizer.sh
./scripts/run_all_tests.sh
./scripts/run_python_tests.sh
./scripts/run_demo.sh
./scripts/clean_generated.sh
```

PowerShell equivalents with the same names and `.ps1` extension are provided for Windows.

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
ChronosLOB-style fixture (`data/models/chronoslob_tiny_fixture.onnx`, input shape `1x4`, output
shape `1x1`) is used only by tests compiled with `ASTERION_HAVE_ONNXRUNTIME`; otherwise the fallback
path is tested. The fixture is deterministic and not trained; its metadata lives beside it in
`data/models/chronoslob_tiny_fixture.metadata.json`, and
`tools/export_chronoslob_tiny_onnx.py` can regenerate or `--verify` it (requires the optional
`onnx` package). Alongside it, a **real** tiny ChronosLOB model
(`data/models/chronoslob_tiny_real.onnx`, input `1x1x4`, output `1x3`) — a trained `DeepLOBModel`
exported from ChronosLOB on **synthetic toy data** — is loaded and validated through the same backend
when ONNX Runtime is present. It is a systems-integration / inference-latency artefact only, with no
predictive-quality, profitability, live-trading or production-serving claim. The ChronosLOB bridge now
also includes a recorded-public-L2 model-contract artefact
(`data/models/chronoslob_public_l2_tiny.onnx`, windowed `1x16x40`→`1x3`, trained on recorded public
Binance crypto L2 depth), validated as a standalone model contract with no predictive or trading
claim. The `ci` workflow has a
manual `onnx_backend` input. When enabled, it runs the `onnx-fallback-manual` lane with
`-DASTERION_USE_ONNXRUNTIME=ON` (dependency absent → deterministic `LinearModel` fallback) and the
`onnx-runtime-manual` lane that downloads the dependency, builds the real backend and runs the
artefacts. Neither lane runs in default CI, and the public-L2 artefact is validated as a model
contract only — not as a predictive-quality, profitability or production-serving claim.

When built with ONNX Runtime, the inference benchmark runner additionally emits `chronoslob_fixture`
and `chronoslob_real` ONNX suites (model load, inference only, and feature-extraction + ChronosLOB
ONNX rows) plus an optional real ChronosLOB ONNX replay-loop row, and two **isolated** rows
(`public_l2_chronoslob_onnx_model_load`, `public_l2_chronoslob_onnx_inference_only`) measuring the
systems cost of scoring the recorded-public-L2 `1x16x40`→`1x3` model-contract artefact. The isolated
rows reproduce the recorded expected output within `1e-3` before timing and are standalone systems-cost
evidence — not the live 4-feature replay-loop contract and not a predictive-quality claim. Default
builds record the replay row and the isolated public-L2 row as skipped/unavailable, and ONNX fallback
is not counted as ONNX evidence. ONNX inference
allocations are measured and reported honestly (model-load is measured separately from steady-state)
and are not claimed to be allocation-free. See
[docs/chronoslob_bridge.md](docs/chronoslob_bridge.md), the fixture report
[reports/chronoslob_onnx_bridge_report_2026_05_31.md](reports/chronoslob_onnx_bridge_report_2026_05_31.md),
the real-model report
[reports/chronoslob_real_model_bridge_report_2026_06_01.md](reports/chronoslob_real_model_bridge_report_2026_06_01.md)
and the recorded-public-L2 report
[reports/chronoslob_public_l2_model_bridge_report_2026_06_04.md](reports/chronoslob_public_l2_model_bridge_report_2026_06_04.md).

The timeout/late-signal policy can also disable the model after repeated late signals when configured
(`InferencePolicyGate` with `disable_on_repeated_late_signals` and `max_consecutive_late_signals`);
that behaviour is unit-tested deterministically with injected timings.

## Risk Audit Trail

The risk gateway can record every accepted or rejected order in an audit trail, capturing timestamp,
client order ID, symbol, deciding check name, decision, reject reason and the relevant limit and
observed values. The trail exposes a deterministic checksum for reproducible comparison across runs.
Recording is opt-in (`set_audit_enabled(true)` or `open_audit_log(...)`) so the pre-trade hot path
stays allocation-free by default. Persistent audit logs are append-only text or JSONL and include
the deterministic audit checksum; they do not add a wall-clock timestamp to that checksum. Rotation
by record count or byte size and checksum verification across rotated files are opt-in. See
[RISK.md](RISK.md).

`generate_audit_manifest(...)` records audit-log file names, byte sizes, record counts, raw content
checksums and cumulative audit-chain checksums. Optional HMAC-SHA256 signing can authenticate the
manifest when the verifier has the same local key. The repository includes only a public test
fixture key; real signing keys must stay outside git. This is tamper-evident tooling, not managed
retention, custody or regulatory storage.

## Simulated Session And Portfolio Risk

`SimulatedBrokerSession` is an in-process deterministic lifecycle model for connect, disconnect,
reconnect, pending cancel, cancel acknowledgment/rejection and fills. It can be attached to a
`RiskGateway` to exercise simulated cancel-on-disconnect exposure release, but it never opens a
network connection or sends live broker messages.

`PortfolioRiskMonitor` is a separate simulated accounting gate. It tracks caller-supplied positions,
fills and marks, then evaluates gross exposure, net exposure, concentration and loss thresholds.
All limits default to disabled and audit recording is opt-in, mirroring the risk gateway.

## Honesty And Limitations

Asterion is a deterministic systems lab. The market-data ingestion path is for recorded and
simulated logs only. It is not connected to any exchange, does not trade live, does not implement
kernel bypass, does not claim true HFT production performance and does not include fabricated
benchmark results. The inference path measures model plumbing and policy accounting only; it is
not a profitable strategy claim. See [LIMITATIONS.md](LIMITATIONS.md) for the full scope statement.

## Example CV Bullet

Built **Asterion**, a C++20 deterministic trading systems lab implementing L3 order book reconstruction, price-time-priority matching, pre-trade risk checks, market replay, execution-report checksums, correctness tests and latency benchmark scaffolding.
