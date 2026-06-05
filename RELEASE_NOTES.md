# Asterion v0.1.0-rc1

**Asterion v0.1.0-rc1: deterministic trading systems lab release candidate**

This is the first release candidate of Asterion, a Linux-first C++20 deterministic
trading systems lab. It is published for review, not for production or live trading.

## Pending v0.1.0 Final Preparation

This pass is **reviewer-facing reproducibility, architecture and
performance-evidence polish**. The repository now includes final-release
preparation that is not yet a release:

- a compact top-of-README benchmark evidence table led by Durham Hamilton8 HPC;
- a one-page [architecture overview](docs/architecture_overview.md);
- an Ubuntu-based devcontainer and
  [reproducible development guide](docs/reproducible_dev_environment.md);
- [GitHub/release metadata guidance](docs/github_release_metadata.md) and an expanded
  final-release checklist.

The devcontainer definition has not been locally built in the Windows shell used
for this preparation because Docker was unavailable. No `v0.1.0` tag or GitHub
release is created by this update.

## Release Metadata

- Release title: **Asterion v0.1.0-rc1: deterministic trading systems lab release candidate**
- Tag: `v0.1.0-rc1` (annotated), pointing at the release commit on `main`.
- Last code/CI-validated commit: `bdb47fff515b911ff26674da3bae1a5d8d280cf1` (`bdb47ff`).
  The tagged commit adds only this `RELEASE_NOTES.md` and a README reviewer-path polish on top;
  it changes no build, test, or runtime code.
- CI run: [ci #26613791388](https://github.com/anannyenaik/asterion/actions/runs/26613791388) — Linux build + test on `bdb47ff`, `success`. Pushing the tag commit triggers a fresh run on the same branch.
- Repository: https://github.com/anannyenaik/asterion

## Demo Command

The reviewer demo runs only on checked-in sample data and writes generated
artifacts under `build/demo/`, which is git-ignored.

```bash
# Linux / macOS
./scripts/configure_release.sh
cmake --build build
./scripts/run_demo.sh --skip-build
```

```powershell
# Windows PowerShell (uses an existing MSYS2/MinGW-w64 toolchain if present)
.\scripts\configure_release.ps1
cmake --build build
.\scripts\run_demo.ps1 -SkipBuild
```

## What Is Implemented

- C++20 CMake project (Ninja-compatible), Linux-first with Windows PowerShell helpers.
- Integer tick prices in the hot path; no floating-point prices in matching or book state.
- L3 order book reconstruction with order-ID lookup, FIFO price-level queues and
  deterministic book checksums, plus book invariant checks.
- Price-time-priority matching for limit, market, cancel and replace flows with
  partial fills, full fills and resting-price execution.
- Structured execution reports with deterministic report checksums.
- Recorded/simulated market data in CSV and a compact ITCH-like binary format, with
  safe rejection of malformed/truncated input and rich replay diagnostics.
- Snapshot event loading that resets and reconstructs the book with deterministic checksums.
- Pre-trade risk gateway: quantity, notional, position, exposure, price-band, stale-data,
  duplicate-ID and kill-switch checks, plus opt-in working-exposure tracking, per-client
  fixed/sliding-window rate limiting, self-trade prevention, replace rechecks, simulated
  cancel-on-kill/cancel-on-disconnect exposure release and append-only audit logging.
- Tamper-evident audit manifests with optional, local-key HMAC-SHA256 signing.
- Simulated broker/session lifecycle state machine and simulated portfolio-risk accounting gate.
- Opt-in shared multi-symbol replay (`MultiSymbolBookSet`) with grouped-vs-shared parity reports;
  grouped single-symbol replay remains the default.
- Measured inference plumbing: `Model`, `LinearModel`, `FeatureExtractor`, timeout/late-signal
  policy hooks, explicit backend selection and an optional ONNX Runtime backend (behind a CMake
  flag) that deterministically falls back to `LinearModel` when the dependency is absent.
- Configurable per-stage latency-budget accounting with stable JSON output.
- Chrono benchmark runner with stable JSON output and allocation counters; optional Google
  Benchmark target behind a CMake flag.
- Python bindings and an inspection CLI (`scripts/asterion_inspect.py`) for replay, checksums,
  diagnostics, audit, risk and benchmark/latency JSON.
- Catch2 unit, golden and randomized property tests; GitHub Actions Linux CI with a demo smoke test.

## What Is Intentionally Not Claimed

- No live exchange, broker or market-data connectivity. The ingestion path is for
  recorded and simulated logs only.
- No production HFT performance, kernel bypass, FPGA, colocated networking or profitability claim.
- No portable benchmark or latency claim. Curated reports contain disclosed
  environment-specific measurements; generated timing JSON remains local and ignored.
- No managed audit retention, custody, compliance guarantee or tamper-proof storage. Audit
  signing is opt-in and local-key based.
- No full portfolio management, market-risk feed or cross-symbol matching engine. The session
  and portfolio-risk components are simulated, in-process accounting models.
- The inference path measures model plumbing and policy accounting only; it is not a strategy
  performance claim.

## Known Limitations

- A snapshot is a sequence of single-order records, not an aggregated L2-only image; levels
  without per-order detail cannot be represented.
- Shared multi-symbol replay is an opt-in parity-tested path, not a cross-symbol matching engine;
  grouped replay is the default until shared parity is exhaustively validated.
- ONNX Runtime is never installed in default CI; only the deterministic fallback is exercised by
  default, with opt-in manual lanes for the real backend.
- Benchmark, latency and regression numbers are machine-dependent and only meaningful when
  produced on the same controlled hardware.
- See [LIMITATIONS.md](LIMITATIONS.md) for the full scope statement.

## Reviewer Guide

1. Read [README.md](README.md), then skim [DESIGN.md](DESIGN.md),
   [CORRECTNESS.md](CORRECTNESS.md), [RISK.md](RISK.md), [BENCHMARKS.md](BENCHMARKS.md) and
   [LIMITATIONS.md](LIMITATIONS.md).
2. Configure and build a release tree, run the C++ and Python test suites:
   ```bash
   ./scripts/configure_release.sh
   cmake --build build
   ctest --test-dir build --output-on-failure
   PYTHONPATH=build/python python -m pytest python/tests
   ```
3. Run the reviewer demo: `./scripts/run_demo.sh --skip-build`.
4. Inspect determinism: the demo and CLI report stable checksums (book, execution report,
   diagnostics, audit chain, latency config) across runs while machine-dependent timings vary.
5. Cross-check the honesty claims above against the code and against [LIMITATIONS.md](LIMITATIONS.md).

See [RELEASE_CHECKLIST.md](RELEASE_CHECKLIST.md) for the full pre-tag verification steps.

## Next Roadmap Items

Carried forward from [ROADMAP.md](ROADMAP.md) and deferred items:

- Optional real ONNX Runtime / LibTorch backend with clean dependency setup, beyond the current
  manual CI lanes.
- Exhaustive shared multi-symbol replay parity sufficient to make shared replay the default.
- Cross-symbol / portfolio-level matching semantics (currently out of scope by design).
- A graphical dashboard beyond the current CLI inspector.
- Live venue/broker session management and managed audit retention/custody — explicitly out of
  scope for this lab and listed only to mark the boundary.

---

This release candidate is for evaluation. It does not trade, connect to any venue,
or claim portable performance.
