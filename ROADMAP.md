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

- richer pre-trade risk controls: open-order (working) exposure, per-client message-rate limiting and
  self-trade prevention, all opt-in and with audit entries;
- snapshot-based book loading for Snapshot events with deterministic checksums and CSV/binary tests;
- explicit inference backend selection with an optional ONNX Runtime backend behind a CMake flag and
  a deterministic LinearModel fallback that keeps the dependency out of normal CI;
- historical benchmark store and cross-run trend reporting, kept out of CI performance gates;
- shared multi-symbol book-set groundwork (single-pass per-symbol routing), without replacing the
  stable grouped single-symbol replay path.

Deferred from Phase 6:

- full shared multi-symbol matching (the book set is groundwork, not a matching engine);
- a sliding-window message-rate limiter (the current limiter is fixed-window);
- cancel-on-kill / cancel-on-disconnect lifecycle handling.

## Phase 7 (candidate)

- promote the multi-symbol book set into a validated shared matching/replay path with diagnostics;
- exercise the ONNX Runtime backend in an opt-in CI lane with a checked-in tiny model fixture;
- order-lifecycle integration so working-order release is driven by execution reports automatically;
- sliding-window rate limiting and cancel-on-kill behaviour;
- persistent, append-only risk audit logs.
