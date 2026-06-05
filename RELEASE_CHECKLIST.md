# Release Checklist

Use this before tagging or presenting the repo for review.

## Required

- Start from a clean checkout: `git status --short --branch`.
- Configure release build: `./scripts/configure_release.sh`.
- Build: `cmake --build build`.
- Run C++ tests: `ctest --test-dir build --output-on-failure`.
- Run Python tests: `PYTHONPATH=build/python python -m pytest python/tests`.
- Build benchmark target: `cmake --build build --target asterion_benchmarks`.
- Run demo: `./scripts/run_demo.sh --skip-build`.
- Build the checked-in devcontainer on a Docker-capable host and run its documented
  Release build/test path; record honestly if the host cannot run Docker.
- Review `README.md`, `DESIGN.md`, `CORRECTNESS.md`, `RISK.md`, `BENCHMARKS.md`,
  `LIMITATIONS.md`, `ROADMAP.md`, `docs/architecture_overview.md` and
  `docs/reproducible_dev_environment.md` for claim drift.
- Re-check `docs/claim_audit.md` and `docs/evidence_index.md` against the code: every
  claim still maps to a passing test or a labelled local report.
- Confirm `reports/README.md` lists every file under `reports/` with its scope and limitation.
- Confirm generated outputs are ignored: benchmark JSON, audit manifests, demo outputs,
  `data/generated/`, `benchmarks/history/` and build directories.
- Confirm every README benchmark-table number matches its linked source report.
- Confirm no generated corpora, profiler output, build artifacts, caches, secrets or
  `.env` files are staged.
- Confirm no live-trading, production-HFT, production model-serving, portable-latency,
  profitability, alpha or predictive-quality claims were added.
- Confirm regular CI and sanitizer CI are green on the exact release commit.
- Review `docs/github_release_metadata.md`; apply description/topics/pinning owner-side
  and update final release notes with the exact commit and CI links.
- Confirm `git diff --check` is clean (no trailing-whitespace / conflict markers).

## Optional

- Debug/sanitizer lane: `./scripts/configure_sanitizer.sh && cmake --build build-debug &&
  ctest --test-dir build-debug --output-on-failure`.
- Manual ONNX lane: run the `ci` workflow with `onnx_backend=true`.
- Manual benchmark workflow: run the `benchmarks` workflow only for local JSON artifacts or
  opt-in Google Benchmark checks.
- Benchmark history: store local JSON under `benchmarks/history/`; do not commit history files.

Do not create the final `v0.1.0` tag until every required item passes. Tag and
GitHub release command guidance is in `docs/github_release_metadata.md`.
