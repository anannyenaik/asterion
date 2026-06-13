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

## Phase 7

- opt-in shared multi-symbol replay path backed by `MultiSymbolBookSet`, with per-symbol summaries,
  diagnostics, combined book checksums and parity tests against grouped replay;
- execution-report-driven working exposure release for partial fills, full fills, cancels, rejects
  and replace flows, with manual `release_order` retained as a fallback;
- fixed-window and sliding-window rate-limit modes, with fixed-window remaining the default;
- cancel-on-kill release of tracked simulated working exposure, plus continued kill-switch rejection
  of new orders;
- append-only text/JSONL risk audit logging, opt-in and separate from the default hot path;
- CLI/Python exposure for shared replay summaries, risk exposure snapshots, audit-log summaries and
  rate-limit mode;
- manual ONNX CI toggle that configures with `-DASTERION_USE_ONNXRUNTIME=ON` while default CI remains
  dependency-free.

Deferred from Phase 7:

- cross-symbol matching or portfolio-level matching semantics;

## Phase 8

- Windows-friendly PowerShell helper scripts that can use an existing MSYS2/MinGW-w64 toolchain;
- optional real ONNX Runtime fixture testing through a tiny checked-in identity model, while default
  CI remains dependency-free and continues to test deterministic fallback behavior;
- simulated disconnect state with opt-in cancel-on-disconnect release, explicit disconnected
  new-order policy, audit entries and Python/CLI summaries;
- richer replace-order risk checks over tracked resting simulated orders, including exposure deltas,
  duplicate command IDs and self-trade prevention;
- deterministic fixed-seed shared replay fuzz/parity tests against grouped replay, with grouped
  replay still the default;
- opt-in audit-log rotation by record count or file size plus checksum verification across rotated
  JSONL/text files;
- broader malformed CSV/binary/replay diagnostics for invalid headers, enums, truncation, bad
  quantities/prices, sequence gaps, timestamp reversals, snapshot misuse and oversized fields;
- CLI/Python exposure for audit verification, shared replay fuzz summaries, disconnect summaries and
  replace-risk outcomes.

Deferred from Phase 8:

- installing ONNX Runtime in default CI;
- exhaustive shared replay parity sufficient to make shared replay the default;
- cross-symbol matching or portfolio-level matching semantics;
- live venue/broker cancel-on-disconnect integration;
- audit retention policy and tamper-proof storage;
- large external malformed-feed corpora.

## Phase 9

- optional real ONNX Runtime CI lane that is manual-only and keeps default CI dependency-free;
- structured grouped-vs-shared replay parity reports, while grouped replay remains the default;
- tamper-evident audit manifests over audit logs, with optional HMAC-SHA256 signing and no committed
  real key material;
- deterministic simulated broker/session lifecycle state machine for connect, disconnect,
  reconnect, pending cancels, cancel acknowledgments/rejections and fills;
- simulated portfolio-risk accounting gate for caller-supplied marks, gross/net exposure,
  concentration and loss thresholds;
- Python bindings and inspection CLI coverage for ONNX status, replay parity and audit manifests;
- documentation that ONNX, audit signing, broker/session lifecycle, shared replay and portfolio risk
  features are optional or simulated as applicable.

Deferred from Phase 9:

- installing ONNX Runtime in default CI;
- making shared replay the default;
- live venue/broker session management;
- managed audit retention/custody or tamper-proof storage;
- full portfolio management, market-risk data or cross-symbol matching semantics.

## Phase 10

- release-candidate demo scripts for shell and PowerShell, using checked-in sample data and ignored
  generated outputs;
- concise release checklist for clean checkout builds, tests, demo, docs and optional lanes;
- CLI error-path hardening for missing files, malformed JSON/logs, invalid options and bad audit
  manifests, with parseable JSON errors when `--json` is requested;
- simulated portfolio-risk fixture and CLI snapshot for the evaluation demo;
- default CI smoke test for the one-command demo, while ONNX and benchmark-history paths remain
  manual or opt-in.

Deferred from Phase 10:

- publishing benchmark numbers;
- live trading or broker/exchange connectivity;
- making optional ONNX Runtime part of default CI;
- managed audit retention/compliance workflows.
