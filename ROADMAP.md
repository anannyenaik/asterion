# Roadmap

## Phase 1

- correctness-first matching engine;
- L3 order book;
- risk gateway;
- deterministic replay.

## Phase 2

- expanded benchmark suite with JSON output;
- optional Google Benchmark target;
- profiling workflow documentation;
- allocation tracking for tests and benchmarks;
- deterministic randomized correctness expansion;
- replay corpus generator modes.

## Phase 3

- recorded/simulated market-data ingestion;
- ITCH-like binary parser and writer;
- saved CSV and binary event logs;
- deterministic simulated adapter modes;
- stronger replay diagnostics and diagnostics checksums.

## Phase 4

- Python bindings for event logs, replay diagnostics and checksum inspection;
- Python examples for recorded/simulated replay, checksum comparison, diagnostics counts and
  benchmark JSON loading;
- measured deterministic linear inference integration with timeout/late-signal policy hooks;
- documented TorchScript-style placeholder interface for a future external runtime;
- feature versioning for L2-derived inference features;
- aggregate multi-symbol replay views built from grouped single-symbol replay summaries.

## Phase 5

- configurable per-stage latency-budget accounting with budget-used/exceeded reporting,
  worst-offender detection and stable JSON output;
- offline benchmark regression comparison with configurable thresholds and new/missing detection;
- replay and benchmark inspection CLI with text and JSON output;
- richer pre-trade risk audit trail with a deterministic audit checksum;
- documentation that benchmark regression and latency results are machine-dependent.

Deferred from Phase 5:

- optional real ONNX Runtime or LibTorch backend once dependency setup is clean;
- a graphical dashboard (the CLI inspector is the current visualizer).

## Phase 6

- optional real ONNX Runtime or LibTorch inference backend behind an explicit CMake flag;
- historical benchmark store and trend reporting on controlled hardware;
- richer risk controls (open-order exposure, message-rate limits, self-trade prevention);
- snapshot-based book loading for Snapshot events;
- shared multi-symbol matching rather than grouped single-symbol replay views.
