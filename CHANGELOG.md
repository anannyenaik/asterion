# Changelog

All notable changes to Asterion are documented here. Asterion is a deterministic
C++20 systems lab for replay, matching, risk, inference integration,
benchmarking and correctness evaluation over recorded and simulated workloads.

The format is loosely based on [Keep a Changelog](https://keepachangelog.com/).
Benchmark/latency numbers are never committed; reports under `reports/` hold
representative local measurements only.

## [Unreleased]

### Added
- Opt-in Clang/libFuzzer robustness targets for bounded binary/CSV event-log
  parsing, replay, matching requests and audit-manifest parsing, with tiny
  curated seed corpora, a manual ASan/UBSan `fuzz-smoke` workflow and
  [FUZZING.md](FUZZING.md). This is robustness evidence only, not production
  safety, real-exchange correctness, live-trading validation or security
  certification.
- Independent Python reference matcher (`asterion.testing.reference_matcher`) and a
  cross-check harness (`asterion.testing.cross_check`) that re-implement Asterion's
  documented matching semantics from scratch and compare the C++ `MatchingEngine`
  against them on deterministic golden and fixed-seed random order flows
  (`python/tests/test_reference_matcher_golden.py`,
  `python/tests/test_reference_matcher_property.py`). Specification-style reference
  testing for the documented contract only — not production-exchange, real-exchange
  completeness or live-trading validation. See [docs/reference_matcher.md](docs/reference_matcher.md).
- Read-only Python bindings for `MatchingEngine` and `CancelOrderRequest` to enable the
  cross-check (data/matching surface only; no live or production exchange surface).

### Changed
- Reworked public documentation, reports, release material and reader-facing
  comments for concise technical presentation. Consolidated repeated warning
  language into explicit scope statements and removed an internal wording draft.

### Validation
- Exercised the documented Dev Container CLI workflow on 2026-06-06 (Dev Container
  CLI 0.87.0, Docker Desktop Engine 29.5.2): `devcontainer up` plus
  `devcontainer exec` ran the documented Release C++ build/`ctest`, the Python
  build/pytest (`157 passed, 1 optional ONNX Runtime test skipped`), the evaluation
  demo and the Clang ASan/UBSan build/`ctest`, all passing. Updated the
  reproducibility docs accordingly; the earlier "`devcontainer up` not separately
  exercised" limitation no longer applies to the current evidence. ONNX Runtime and
  heavy benchmarks remained optional and were not part of this validation.

## [0.1.0] - 2026-06-05

Final reproducibility, release hygiene and publication pass.

### Added
- Final architecture, evidence, matching-semantics and release
  documentation across the work completed after `v0.1.0-rc2`.
- Automatic ASan/UBSan push/PR gating, public-L2 ChronosLOB model-contract
  evidence and the isolated C++ optional-ONNX systems-cost benchmark.

### Changed
- Re-centred the performance narrative on Durham Hamilton8 HPC evidence while
  retaining Windows/MSYS2 and WSL2 as historical/local development baselines.
- Polished matching/order semantics, CI visibility and the reproducible
  development environment.
- Corrected the Ubuntu 24.04 Dockerfile to handle an existing UID/GID 1000 and
  install Clang's sanitizer runtime explicitly.

### Validation
- Validated the checked-in Dockerfile and documented Docker-only path on one
  local Docker Desktop Linux-container installation: GCC Release C++, Python
  bindings/pytest/demo and Clang ASan/UBSan all passed.
- The Dev Container CLI was unavailable on the validating Windows host, so
  `devcontainer up` was not separately exercised.
- No heavy benchmark or default-image ONNX Runtime validation was performed.
- This validation is local development reproducibility evidence, not
  production readiness or a portable benchmark claim.

## [0.1.0-rc2] — 2026-06-01 (since v0.1.0-rc1)

Tagged at commit `92901f7`; published as a GitHub draft prerelease. Verified by the Linux
CI build+test workflow (run `26775429417`, green) plus a local fresh-build gate.

Features added on top of `v0.1.0-rc1`. All are opt-in or default-safe; deterministic
single-thread replay remains the default path.

### Added
- Optimized measured hot path (binary replay → L3 update → reusable L2 → strategy → risk)
  with a documented local benchmark report.
- Measured ONNX/inference benchmark path and a configurable late-signal model-disable policy gate.
- Recorded **public** Binance depth-stream case study: deterministic L2→synthetic-L3 normaliser,
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
- Documentation and evidence pass: `docs/claim_audit.md` (claim→evidence map),
  `docs/evidence_index.md` (technical question → file + command), `reports/README.md` (report
  scope/limitation index), this `CHANGELOG.md` and README polish.
  No runtime, build or test code changed in this pass.

### Scope
- No live exchange/broker connectivity, order placement, kernel bypass, FPGA or colocated networking.
- No profitability/alpha claim; inference measures plumbing only.
- No portable/committed benchmark numbers.
- Native-Linux `perf` counter evidence remains pending: blocked on the current host by hardware
  virtualization being disabled in firmware (BIOS/UEFI), so WSL2 cannot boot a Linux kernel.
  Not a release blocker; deferred to native/cloud Linux. See
  [reports/linux_performance_evaluation_2026_05_31.md](reports/linux_performance_evaluation_2026_05_31.md).

## [0.1.0-rc1] — see [RELEASE_NOTES.md](RELEASE_NOTES.md)

First release candidate: deterministic replay, L3 book, price-time-priority matching,
execution reports, pre-trade risk gateway, audit trail/manifests, simulated session and
portfolio risk, measured inference plumbing, latency-budget accounting, benchmark runner,
Python bindings + inspection CLI, and Linux CI.
