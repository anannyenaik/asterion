# Asterion v0.1.0

**Final reproducibility, release hygiene and v0.1.0 publication.**

Asterion v0.1.0 is a Linux-first C++20 deterministic trading systems lab for
recorded/simulated market replay, L3 order-book and matching semantics, risk
controls, telemetry, and measured optional inference integration. It is
published for reproducible systems evaluation over recorded and simulated
workloads.

## Release Highlights

- Deterministic CSV and compact binary event-log replay, with a documented and
  guarded event-log schema v1 plus stable book, execution-report, diagnostics,
  event-log, audit and configuration checksums.
- L3 order-book reconstruction with order-ID lookup, FIFO price-level queues,
  invariant checks, snapshots and deterministic checksums.
- Price-time-priority matching with limit/market/cancel/replace flows and
  documented IOC, FOK, post-only, self-trade-prevention and order-state
  semantics.
- Pre-trade risk gateway, opt-in working-exposure and rate-limit controls,
  simulated session/portfolio accounting, append-only audit trail and optional
  local-key tamper-evident manifests.
- Python bindings, inspection CLI and a one-command evaluation demo over checked-in
  sample data.
- Default GitHub Actions matrix covering GCC Release, Clang Release, Python
  bindings/pytest/demo and automatic Clang ASan/UBSan gating.
- Optional manual ONNX Runtime lanes with deterministic `LinearModel` fallback
  when the dependency is absent.
- Recorded-public-L2 ChronosLOB model-contract artefact and isolated C++ ONNX
  systems-cost benchmark. These are integration evidence only, with no
  predictive-quality, alpha, profitability or production-serving claim.

## Reproducibility Validation

The checked-in Ubuntu 24.04 Docker/devcontainer path was validated on
**2026-06-05** using a local Docker Desktop Linux-container installation
(Docker Desktop 4.76.0, Engine 29.5.2). The image provides GCC 13.3, Clang 18.1,
CMake 3.28, Ninja 1.11, Python 3.12 and pytest 7.4.

The locally built image passed:

- GCC Release configure/build and `ctest`;
- Python-enabled GCC Release configure/build, `ctest`, pytest
  (`108 passed, 1 optional ONNX Runtime test skipped`) and the evaluation demo;
- Clang Debug ASan/UBSan configure/build and `ctest`.

On the Windows host, a fresh MSYS2/UCRT64 GCC Release build pinned to Python
3.11 also passed `ctest`, all 109 Python tests and the PowerShell evaluation demo.
The host MSYS2 sanitizer build could not link because that local toolchain does
not provide `libasan`/`libubsan`; sanitizer coverage instead passed in the local
Clang container above and in the hosted gating workflow.

The Dockerfile was corrected during validation to handle Ubuntu 24.04's existing
UID/GID 1000 and to install the Clang sanitizer runtime explicitly. The
Dev Container CLI itself was not installed on the validating Windows host, so
`devcontainer up` was not separately exercised; the checked-in
`devcontainer.json` uses the validated Dockerfile and matching workspace/user
settings. This is one local reproducibility validation, not a portable or
production-deployment guarantee. ONNX Runtime and heavy benchmarks remain
outside the default image and were not run.

See [docs/reproducible_dev_environment.md](docs/reproducible_dev_environment.md)
and [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md).

## Performance And Model-Contract Evidence

- The **Durham Hamilton8 HPC** report remains the primary performance context:
  [durham_hpc_performance_evaluation_2026_06_04.md](reports/durham_hpc_performance_evaluation_2026_06_04.md).
- Windows/MSYS2 and WSL2 results remain historical/local development baselines.
  Cross-machine comparison is not meaningful.
- The public-L2 ChronosLOB bridge remains a systems/model-contract artefact:
  [chronoslob_public_l2_model_bridge_report_2026_06_04.md](reports/chronoslob_public_l2_model_bridge_report_2026_06_04.md).
- The optional inference event-loop report measures plumbing and policy
  accounting only:
  [inference_event_loop_cost_report_2026_06_01.md](reports/inference_event_loop_cost_report_2026_06_01.md).

All benchmark and latency evidence is representative and environment-specific.
No new benchmark numbers were generated for this release pass.

## Scope And Limitations

- No live exchange, authenticated exchange, broker or order-placement
  connectivity.
- No production readiness, production-HFT, portable-latency, profitability,
  alpha or predictive-quality claim.
- No production model-serving claim. ONNX Runtime is optional; the deterministic
  `LinearModel` fallback remains available.
- Deterministic single-thread replay and the correctness-first `OrderBook` /
  full-validation path remain the defaults. Optional SPSC, pooled-book and ONNX
  paths do not replace them.
- Recorded Binance public depth is L2 data adapted with deterministic synthetic
  order IDs; it is not real L3 identity or equities-market realism.
- Audit tooling is not managed retention, custody or a compliance guarantee.

See [LIMITATIONS.md](LIMITATIONS.md) and
[docs/claim_audit.md](docs/claim_audit.md) for the full boundaries.

## Project Links

- [README](README.md)
- [Architecture overview](docs/architecture_overview.md)
- [Performance evidence summary](reports/performance_evidence_summary_2026_06_01.md)
- [Durham HPC report](reports/durham_hpc_performance_evaluation_2026_06_04.md)
- [Public-L2 ChronosLOB bridge report](reports/chronoslob_public_l2_model_bridge_report_2026_06_04.md)
- [Inference event-loop report](reports/inference_event_loop_cost_report_2026_06_01.md)
- [Matching semantics](docs/matching_semantics.md)
- [Claim audit](docs/claim_audit.md)
- [Release checklist](RELEASE_CHECKLIST.md)

The final annotated `v0.1.0` tag points at the exact verified release commit.
Default and sanitizer CI run links for that commit are recorded in the published
GitHub release.
