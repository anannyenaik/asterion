# Asterion v0.1.0-rc2 (release-candidate notes — draft)

**Asterion v0.1.0-rc2: deterministic trading systems lab — second release candidate**

Asterion is a deterministic C++20 trading systems lab for replay, matching, risk,
inference integration, benchmarking and correctness evaluation. It is published for
review, not for production or live trading. These notes are a **draft**; no tag has
been created.

## Status of this draft

- Previous tag: `v0.1.0-rc1` (annotated) at commit `bdb47ff`.
- This candidate covers commits `b0c6c7a..HEAD` on `main` (the rc1 notes commit through
  the documentation/evidence polish).
- Last locally verified commit: see the verification summary below; the CI source of
  truth is the Linux build+test workflow on `main`.
- No GitHub release has been published and no tag has been created for rc2.

## Recommendation: tag `v0.1.0-rc2` now

There are substantial, review-relevant features since `v0.1.0-rc1` (ONNX/inference
benchmark path + late-signal policy, Binance recorded-depth case study, schema hardening,
pooled-book allocation path, caller-owned feature buffer, ChronosLOB ONNX bridge, the
opt-in SPSC pipeline and steady-state harness). These warrant a fresh review checkpoint.

Native-Linux `perf` counter evidence is **not** a blocker for an `rc2` checkpoint: it is
an additive, optional, local-only artefact that is honestly marked pending. Cutting
`v0.1.0-rc2` now and deferring `perf` evidence to the final `v0.1.0` (or a later rc) is
the recommended path — do **not** hold the release candidate on `perf`.

> Tagging and publishing are deferred until explicitly requested.

## Major features since v0.1.0-rc1

- **Hot-path optimization** with a documented local benchmark report.
- **Measured ONNX/inference benchmark path** and a configurable late-signal model-disable policy.
- **Recorded public Binance depth case study** — honest L2→synthetic-L3 normaliser, deterministic
  replay, fixture regeneration guard. Manual/opt-in capture, never in CI.
- **Event-log schema v1 hardening** — JSON schema file + drift/contract tests.
- **Opt-in `PooledOrderBook`** allocation path with stress-parity validation.
- **Larger-corpus performance-evaluation path** + ready-to-run Linux `perf` helper.
- **Caller-owned zero-allocation inference feature buffer**.
- **ChronosLOB-style ONNX bridge fixture** (tiny, deterministic, not trained) + regen tool.
- **Optional ONNX Runtime backend** with deterministic `LinearModel` fallback; manual-only CI lanes.
- **Opt-in SPSC replay pipeline** — consumer reuses the single-thread `ReplayEngine`, so
  book/execution/diagnostics checksums are bit-identical regardless of thread timing.
- **Opt-in steady-state SPSC harness** + `ReplayValidationMode::Light` for large-corpus throughput.

## Verification summary

Run the standard gate before tagging (see [RELEASE_CHECKLIST.md](../RELEASE_CHECKLIST.md)):

```bash
./scripts/configure_release.sh
cmake --build build
ctest --test-dir build --output-on-failure
PYTHONPATH=build/python python -m pytest python/tests
cmake --build build --target asterion_benchmarks
./scripts/run_demo.sh --skip-build
```

Latest local verification (Windows / MSYS2 UCRT64 toolchain, single pinned interpreter):
C++ suite passed (167 Catch2 cases), Python suite passed (92 tests), benchmark target
built and ran, demo reproduced deterministic checksums. Default Linux CI is the source of
truth for green status.

## Known limitations

See [LIMITATIONS.md](../LIMITATIONS.md) and [docs/claim_audit.md](claim_audit.md). Headlines:

- Native-Linux `perf` counter evidence pending (postponed until native Linux / WSL access).
- All benchmark/latency numbers are representative local measurements, not portable.
- ONNX Runtime is never installed in default CI; only the deterministic fallback runs by default.
- Snapshots are single-order records, not aggregated L2-only images.
- Shared multi-symbol replay is opt-in/parity-tested; grouped replay is the default.

## Claim boundaries (unchanged)

- No live exchange/broker/market-data connectivity; recorded/simulated logs only.
- No order placement, authenticated connectivity, kernel bypass, FPGA or colocated networking.
- No production-HFT performance claim and no profitability/alpha claim.
- No managed audit retention/custody/compliance; signing is opt-in, local-key only.
- No cross-symbol matching engine.

## Optional / advanced paths

- ONNX Runtime backend: `-DASTERION_USE_ONNXRUNTIME=ON` (see [docs/chronoslob_bridge.md](chronoslob_bridge.md)).
- Pooled order book: opt-in benchmark/test path (see allocation reports).
- SPSC pipeline: `run_spsc_replay` / `run_spsc_replay_steady_state` (see SPSC reports).
- Linux `perf`: `scripts/run_linux_perf_profile.sh` (pending native Linux/WSL completion).
