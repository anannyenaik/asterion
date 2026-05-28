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
- notebooks for recorded/simulated replay and latency analysis;
- ONNX or TorchScript-style inference integration;
- feature versioning;
- aggregate multi-symbol replay views.

## Phase 5

- benchmark regression thresholds if enough historical data exists;
- latency budget accounting;
- dashboard or CLI visualizer;
- richer risk audit trail.
