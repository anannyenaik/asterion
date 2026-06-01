# Reviewer Evidence Index

This maps common reviewer questions to the file or command that answers them. Pair it
with [claim_audit.md](claim_audit.md), which classifies the strength of each claim.

Conventions:
- C++ tests: [`tests/unit/`](../tests/unit), run via `ctest --test-dir build --output-on-failure`.
- Python tests: [`python/tests/`](../python/tests), run via `PYTHONPATH=build/python python -m pytest python/tests`.
- Reports: [`reports/`](../reports) — every report is **representative local measurement**, not portable.
- "Local-only / optional" caveats are flagged per row.

## Build, test and demo

**Q: How do I run the 10-minute reviewer demo?**
- Answer: configure Release, build, run C++ + Python tests, run the demo on checked-in data.
- Commands (Linux/macOS):
  ```bash
  ./scripts/configure_release.sh
  cmake --build build
  ctest --test-dir build --output-on-failure
  PYTHONPATH=build/python python -m pytest python/tests
  ./scripts/run_demo.sh --skip-build
  ```
- Windows PowerShell: `.\scripts\configure_release.ps1`; `cmake --build build`; `.\scripts\run_demo.ps1 -SkipBuild`.
- Caveat: use **one** Python interpreter for both the build and the tests/demo — the
  compiled extension is ABI-specific (see "Why does pytest fail to import asterion?").

## Determinism and replay

**Q: Where is deterministic replay tested?**
- Answer: replay produces stable book / execution-report / diagnostics / event-log checksums.
- Files: `tests/unit/test_event_log_replay.cpp`, `python/tests/test_replay_stability.py`.
- Reproduce: `ctest --test-dir build -R asterion_tests`; or `./build/asterion_replay --input data/samples/sample_replay.csv`.

**Q: Where is CSV↔binary parity tested?**
- Files: `tests/unit/test_event_log_replay.cpp` ("CSV and binary event logs preserve ordering and replay checksums"), `python/tests/test_binance_normalise.py::test_csv_binary_equivalence_and_replay_checksums`.

## Schema stability

**Q: Where are the binary/CSV schema guards documented?**
- Answer: a frozen v1 wire contract with a JSON schema file + drift tests.
- Files: [`data/schema/event_log_schema_v1.json`](../data/schema/event_log_schema_v1.json), [docs/event_log_schema.md](event_log_schema.md).
- Tests: `tests/unit/test_event_log_replay.cpp` ("Event-log schema constants match the documented v1 wire contract", "Binary event-log writer preserves v1 header and record field layout"); `python/tests/test_event_log_schema.py`.

## Matching, risk and audit

**Q: Where is price-time-priority matching tested?**
- Files: `tests/unit/test_order_book.cpp`, `tests/unit/test_risk_gateway.cpp`.

**Q: Where is the risk gateway / audit trail?**
- Files: `tests/unit/test_risk_gateway.cpp`, `test_risk_controls.cpp`, `test_risk_audit.cpp`; `python/tests/test_risk_tooling.py`. See [RISK.md](../RISK.md).
- Reproduce: `PYTHONPATH=build/python python scripts/asterion_inspect.py audit-summary --input data/samples/sample_risk_audit.jsonl --json`.

**Q: Where are audit manifests / tamper-evidence tested?**
- Files: `tests/unit/test_audit_manifest.cpp` (detects truncated/edited/missing/reordered files; HMAC RFC vector).
- Reproduce: `PYTHONPATH=build/python python scripts/asterion_inspect.py audit-manifest --input data/samples/sample_risk_audit.jsonl --output build/m.jsonl --json` then `audit-manifest-verify`.
- Caveat: signing is opt-in, local-key only; not managed retention/custody.

## Allocation and performance

**Q: Where are allocation results?**
- Answer: scoped, warmed allocation tests + local before/after reports.
- Tests: `tests/unit/test_allocation_tracking.cpp`, `test_hot_path.cpp`, `test_pooled_order_book.cpp`, `test_telemetry_inference.cpp` (caller-owned buffer).
- Reports: [allocation_optimisation_report](../reports/allocation_optimisation_report_2026_05_31.md), [pooled_order_book_stress_report](../reports/pooled_order_book_stress_report_2026_05_31.md), [inference_feature_buffer_report](../reports/inference_feature_buffer_report_2026_05_31.md).
- Caveat: zero-allocation claims apply only after explicit warm-up in the disclosed paths; numbers are local.

**Q: Where are the benchmarks / how do I regenerate JSON?**
- Build + run: `cmake --build build --target asterion_benchmarks`; `./build/asterion_benchmarks --json build/asterion_benchmark.json --no-text`.
- Report: [benchmark_report](../reports/benchmark_report_2026_05_31.md). See [BENCHMARKS.md](../BENCHMARKS.md).
- Caveat: machine-dependent; no benchmark JSON is committed.

**Q: Which JSON output fields are stable vs machine-dependent?**
- Answer: see [docs/json_outputs.md](json_outputs.md) — each stable JSON output mapped to its
  guarding test, with deterministic fields (checksums/counts) separated from machine-dependent
  timings. Never assert on nanosecond/throughput fields.

**Q: Where is the PooledOrderBook result?**
- Tests: `tests/unit/test_pooled_order_book.cpp`. Reports: allocation_optimisation + pooled_order_book_stress.
- Caveat: opt-in; correctness-first `OrderBook` is the default.

## Inference / ONNX

**Q: Where is ONNX tested?**
- Files: `tests/unit/test_inference_backend.cpp` (fallback always tested; real backend behind `ASTERION_HAVE_ONNXRUNTIME`).
- Reproduce fallback (default build): `ctest --test-dir build -R asterion_tests`.
- Reproduce real backend: configure with `-DASTERION_USE_ONNXRUNTIME=ON` (requires ONNX Runtime).
- Caveat: optional; not in default CI.

**Q: Where is the ChronosLOB bridge?**
- Files: `data/models/chronoslob_tiny_fixture.onnx` (+ `.metadata.json`), `tools/export_chronoslob_tiny_onnx.py`, [docs/chronoslob_bridge.md](chronoslob_bridge.md).
- Tests: `tests/unit/test_inference_backend.cpp` (ChronosLOB cases), `python/tests/test_chronoslob_bridge.py`.
- Report: [chronoslob_onnx_bridge_report](../reports/chronoslob_onnx_bridge_report_2026_05_31.md).
- Caveat: fixture is deterministic and **not trained**; no predictive/profitability claim.

## Concurrency (SPSC)

**Q: Where is SPSC parity tested?**
- Answer: consumer reuses the single-thread `ReplayEngine`, so checksums are bit-identical to single-thread regardless of thread timing.
- Files: `tests/unit/test_spsc_replay.cpp`, `test_spsc_ring_buffer.cpp`; `python/tests/test_spsc_replay.py`.
- Reproduce: `PYTHONPATH=build/python python scripts/run_spsc_replay_demo.py --input data/samples/sample_replay.csv --queue-capacity 4 --json`.
- Report: [spsc_replay_pipeline_report](../reports/spsc_replay_pipeline_report_2026_05_31.md).

**Q: Where is the steady-state SPSC throughput harness?**
- Files: `tests/unit/test_spsc_replay.cpp` (steady-state cases). Report: [spsc_steady_state_report](../reports/spsc_steady_state_report_2026_05_31.md).
- Reproduce: `./build/asterion_benchmarks --only-steady-state-replay --dataset <generated.bin> --steady-state-validation-mode light --spsc-queue-capacity 4096`.
- Caveat: opt-in; `Full` validation is the default; generated corpora are git-ignored.

## Market data

**Q: Where is the Binance case study?**
- Files: `tools/normalise_binance_depth_to_asterion.py`, `data/samples/binance_depth_sample.raw.jsonl`, [docs/market_data.md](market_data.md).
- Tests: `python/tests/test_binance_normalise.py`. Report: [binance_replay_case_study](../reports/binance_replay_case_study_2026_05_31.md).
- Reproduce: normalise the fixture to CSV/binary, then `./build/asterion_replay --input build/binance_sample.bin --format binary` (see README "Recorded Binance Public Depth Case Study").
- Caveat: recorded public depth demo; not live trading, not authenticated, not equities-market realism.

## Scope and limitations

**Q: Where are limitations stated?**
- File: [LIMITATIONS.md](../LIMITATIONS.md) (full scope statement); per-component notes also live at definition sites and in [claim_audit.md](claim_audit.md).

**Q: What is the Linux perf status?**
- Answer: methodology + helper script exist; counter values are postponed until native Linux / WSL access and are not fabricated.
- Files: [reports/perf_profile.md](../reports/perf_profile.md), [reports/linux_performance_evaluation_2026_05_31.md](../reports/linux_performance_evaluation_2026_05_31.md), `scripts/run_linux_perf_profile.sh`.
- Caveat: local-only / pending native Linux.

## Toolchain note (Windows)

**Q: Why does pytest fail to import `asterion` after a successful build?**
- Answer: the compiled `_native` extension is ABI-specific to the Python that built it.
  An MSYS2/UCRT64 build produces a `cp3XX-mingw` `.pyd` that a different system Python
  cannot load. Build and run tests/demo with the **same** interpreter (pass
  `-PythonExe` to the PowerShell scripts to pin it). Default Linux CI uses one
  interpreter throughout and is unaffected.
