# Changelog

All notable changes to Asterion are documented here. Asterion is a deterministic
C++20 trading systems lab for replay, matching, risk, inference integration,
benchmarking and correctness evaluation — not a live trading or production-HFT system.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/).
Benchmark/latency numbers are never committed; reports under `reports/` hold
representative local measurements only.

## [Unreleased]

### Added
- `docs/claim_audit.md` — every major claim classified against its evidence.
- `docs/evidence_index.md` — reviewer questions mapped to files and reproduction commands.
- `reports/README.md` — index of all reports with scope, optional-dependency and limitation columns.
- `CHANGELOG.md` and `docs/cv_summary_draft.md`.
- README reviewer-facing polish (claim-boundary links, evidence pointers).

### Notes
- No runtime, build or test code changed in this documentation/evidence pass.

## [0.1.0-rc2] — candidate (since v0.1.0-rc1)

Features added on top of `v0.1.0-rc1`. All are opt-in or default-safe; deterministic
single-thread replay remains the default path.

### Added
- Optimized measured hot path (binary replay → L3 update → reusable L2 → strategy → risk)
  with a documented local benchmark report.
- Measured ONNX/inference benchmark path and a configurable late-signal model-disable policy gate.
- Recorded **public** Binance depth-stream case study: honest L2→synthetic-L3 normaliser,
  deterministic replay, and a fixture regeneration guard. Capture is manual/opt-in, never in CI.
- Event-log schema v1 boundary hardening: JSON schema file plus drift/contract tests.
- Opt-in `PooledOrderBook` L3 benchmark path with allocation-reduction and stress-parity validation.
- Larger-corpus replay performance-evaluation path and a ready-to-run Linux `perf` helper
  (with the WSL2 `perf` blocker documented; counter values are not fabricated).
- Caller-owned zero-allocation inference feature buffer path.
- ChronosLOB-style ONNX bridge fixture (tiny deterministic 1×4→1×1 artefact, metadata, regen tool).
- Optional ONNX Runtime backend selection with deterministic `LinearModel` fallback;
  explicit ONNX Runtime root lookup; manual-only CI lanes.
- Opt-in bounded single-producer/single-consumer (SPSC) replay pipeline whose consumer reuses
  the single-thread `ReplayEngine`, giving bit-identical checksums regardless of thread timing;
  lossless-blocking backpressure by default, opt-in `DropNewestOnFull` for overload experiments.
- Opt-in steady-state SPSC replay evaluation and `ReplayValidationMode::Light` for large-corpus
  throughput measurement; `Full` validation remains the default correctness path.

### Not changed / still out of scope
- No live exchange/broker connectivity, order placement, kernel bypass, FPGA or colocated networking.
- No profitability/alpha claim; inference measures plumbing only.
- No portable/committed benchmark numbers.
- Native-Linux `perf` counter evidence remains pending (postponed until native Linux / WSL access).

## [0.1.0-rc1] — see [RELEASE_NOTES.md](RELEASE_NOTES.md)

First release candidate: deterministic replay, L3 book, price-time-priority matching,
execution reports, pre-trade risk gateway, audit trail/manifests, simulated session and
portfolio risk, measured inference plumbing, latency-budget accounting, benchmark runner,
Python bindings + inspection CLI, and Linux CI.
