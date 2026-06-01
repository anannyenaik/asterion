# Asterion v0.1.0-rc2 (release-candidate notes)

**Asterion v0.1.0-rc2: deterministic trading systems lab — second release candidate**

Asterion is a deterministic C++20 trading systems lab for replay, matching, risk,
inference integration, benchmarking and correctness evaluation. It is published for
review, not for production or live trading.

## Release status

- Release commit: `92901f72bde54ecb81c637523a0aed83ea64a15a` (`92901f7`) on `main`.
- Verification date: 2026-06-01.
- CI source of truth (Linux build+test on `main`): run
  [`26775429417`](https://github.com/anannyenaik/asterion/actions/runs/26775429417) — success.
- Previous tag: `v0.1.0-rc1` (annotated) at commit `bdb47ff`.
- This candidate covers commits `b0c6c7a..92901f7` on `main` (the rc1 notes commit through
  the documentation/evidence polish).
- This is a **prerelease** review checkpoint, published as a GitHub draft prerelease.

## Why rc2

There are substantial, review-relevant features since `v0.1.0-rc1` (ONNX/inference
benchmark path + late-signal policy, Binance recorded-depth case study, schema hardening,
pooled-book allocation path, caller-owned feature buffer, ChronosLOB ONNX bridge, the
opt-in SPSC pipeline and steady-state harness). These warrant a fresh review checkpoint.

Native-Linux `perf` counter evidence is **not** a blocker for this `rc2` checkpoint: it is
an additive, optional, local-only artefact that is honestly marked pending (see
[Known limitations](#known-limitations)). It is deferred to the final `v0.1.0` (or a later
rc) and does not gate this release candidate.

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

Local verification of the release commit `92901f7` (2026-06-01, Windows / MSYS2 UCRT64
toolchain — cmake 4.3.3, g++ 16.1.0, Ninja — with a single pinned interpreter, CPython
3.11.9, matching the built extension):

- Fresh Release configure + build with Python bindings: clean.
- C++ suite: CTest 1/1 passed (114915 assertions across 167 Catch2 cases).
- Python suite: 92 passed.
- Benchmark target `asterion_benchmarks`: built and ran (23 benchmark rows).
- One-command demo: passed and reproduced deterministic checksums.
- `git diff --check`: clean.

Toolchain caveat (Windows): the compiled Python extension is ABI-specific to the
interpreter that built it. Build and run pytest/the demo with the **same** interpreter
(here CPython 3.11 throughout). Default Linux CI uses one interpreter end-to-end and is the
source of truth for green status; CI run
[`26775429417`](https://github.com/anannyenaik/asterion/actions/runs/26775429417) is green
on this commit.

## Reports and evidence

- [docs/claim_audit.md](claim_audit.md) — every major claim classified against its evidence.
- [docs/evidence_index.md](evidence_index.md) — reviewer questions → file + reproduction command.
- [reports/README.md](../reports/README.md) — report-by-report scope, optional deps, limitations.
- [reports/benchmark_report_2026_05_31.md](../reports/benchmark_report_2026_05_31.md) — local hot-path benchmark.
- [reports/linux_performance_evaluation_2026_05_31.md](../reports/linux_performance_evaluation_2026_05_31.md) — larger-corpus eval + perf blocker.
- [reports/spsc_replay_pipeline_report_2026_05_31.md](../reports/spsc_replay_pipeline_report_2026_05_31.md) and [reports/spsc_steady_state_report_2026_05_31.md](../reports/spsc_steady_state_report_2026_05_31.md) — SPSC parity/throughput.
- [reports/binance_replay_case_study_2026_05_31.md](../reports/binance_replay_case_study_2026_05_31.md) — recorded public depth case study.
- [reports/chronoslob_onnx_bridge_report_2026_05_31.md](../reports/chronoslob_onnx_bridge_report_2026_05_31.md) — optional ONNX bridge.

## Known limitations

See [LIMITATIONS.md](../LIMITATIONS.md) and [docs/claim_audit.md](claim_audit.md). Headlines:

- Native-Linux `perf` counter evidence pending: blocked on this host because hardware
  virtualization is disabled in the machine firmware (BIOS/UEFI), so WSL2 cannot boot a
  Linux kernel and the PMU is not exposed. Documented in
  [reports/linux_performance_evaluation_2026_05_31.md](../reports/linux_performance_evaluation_2026_05_31.md),
  [reports/perf_profile.md](../reports/perf_profile.md), [docs/profiling.md](profiling.md),
  [docs/claim_audit.md](claim_audit.md) and [docs/evidence_index.md](evidence_index.md).
  Unblocked by enabling Intel VT-x / AMD-V in firmware or using a native/cloud Linux host.
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
- Linux `perf`: `scripts/run_linux_perf_profile.sh` (pending firmware virtualization / native Linux host).
