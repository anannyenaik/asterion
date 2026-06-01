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
- Review `README.md`, `DESIGN.md`, `CORRECTNESS.md`, `RISK.md`, `BENCHMARKS.md`,
  `LIMITATIONS.md` and `ROADMAP.md` for claim drift.
- Re-check `docs/claim_audit.md` and `docs/evidence_index.md` against the code: every
  claim still maps to a passing test or a labelled local report.
- Confirm `reports/README.md` lists every file under `reports/` with its scope and limitation.
- Confirm generated outputs are ignored: benchmark JSON, audit manifests, demo outputs,
  `data/generated/`, `benchmarks/history/` and build directories.
- Confirm no benchmark numbers, live-trading claims or untested performance claims were added.
- Confirm `git diff --check` is clean (no trailing-whitespace / conflict markers).

## Optional

- Debug/sanitizer lane: `./scripts/configure_sanitizer.sh && cmake --build build-debug &&
  ctest --test-dir build-debug --output-on-failure`.
- Manual ONNX lane: run the `ci` workflow with `onnx_backend=true`.
- Manual benchmark workflow: run the `benchmarks` workflow only for local JSON artifacts or
  opt-in Google Benchmark checks.
- Benchmark history: store local JSON under `benchmarks/history/`; do not commit history files.
