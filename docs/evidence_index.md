# Reviewer Evidence Index

This maps common reviewer questions to the file or command that answers them. Pair it
with [claim_audit.md](claim_audit.md), which classifies the strength of each claim.

Conventions:
- C++ tests: [`tests/unit/`](../tests/unit), run via `ctest --test-dir build --output-on-failure`.
- Python tests: [`python/tests/`](../python/tests), run via `PYTHONPATH=build/python python -m pytest python/tests`.
- Reports: [`reports/`](../reports) — every report is **representative local measurement**, not portable.
- "Local-only / optional" caveats are flagged per row.

## Build, test and demo

**Q: Is there a reproducible Ubuntu development environment?**
- Answer: yes, a lightweight devcontainer installs GCC, Clang, CMake, Ninja,
  Python/pytest, git and basic build tools. It installs no ONNX Runtime and runs no
  build, test or benchmark automatically.
- Files: [reproducible_dev_environment.md](reproducible_dev_environment.md),
  [`.devcontainer/Dockerfile`](../.devcontainer/Dockerfile),
  [`.devcontainer/devcontainer.json`](../.devcontainer/devcontainer.json).
- Validation: the Dockerfile and documented Docker-only path were built and run
  on a local Docker Desktop Linux-container installation on 2026-06-05. GCC
  Release C++, Python/pytest/demo and Clang ASan/UBSan paths passed after fixing
  UID/GID handling and explicitly installing Clang's sanitizer runtime.
- Caveat: the Dev Container CLI was unavailable on the validating Windows host,
  so `devcontainer up` was not separately exercised. This is one local
  reviewer/development validation, not a portable or production guarantee.

**Q: Where is the one-page architecture view?**
- Answer: [architecture_overview.md](architecture_overview.md) maps the main
  replay/book/strategy/risk/matching/report path and the optional SPSC, pooled,
  ONNX, Python and audit-manifest side paths.
- Caveat: it is a systems-lab architecture, not a live-trading or production
  deployment diagram.

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

**Q: What does CI run, and what does each lane prove?**
- Answer: the default checks run on every push/PR to `main` and are dependency-light.
  Reviewer-readable jobs:
  - `gcc-release` — C++20 Release build + `ctest` on GCC (strict warnings).
  - `clang-release` — same build + tests on Clang → cross-compiler portability.
  - `python-bindings` — builds the bindings, runs `pytest`, the inspection CLI and the
    one-command demo on a single interpreter.
  - `asan-ubsan` in [.github/workflows/sanitizers.yml](../.github/workflows/sanitizers.yml)
    — Debug unit/golden/property suite under Address + UndefinedBehaviour sanitizers,
    including the full public-L2 metadata/model-contract fixture. Numeric arrays use
    bounded iterative parsing, so the lane needs no raised stack limit.
- **Manual-only** (never gate `main`):
  - `fuzz-smoke` in [.github/workflows/fuzz-smoke.yml](../.github/workflows/fuzz-smoke.yml)
    — bounded Clang/libFuzzer + ASan/UBSan runs over parser/replay/matching/audit
    targets.
  - `onnx-fallback-manual` and `onnx-runtime-manual` in the `ci` workflow (dispatch with
    `onnx_backend=true`); ONNX Runtime is never installed by default CI.
  - `benchmarks` and `linux-performance` workflows (never gate on numbers).
- What default CI proves: the project builds clean on two compilers; the C++/Python
  suites plus the reviewer demo pass; and the tested C++ paths pass ASan/UBSan.
- What it does **not** prove: any performance number (benchmarks are reported, not
  gated), predictive quality, live connectivity, production model serving or production
  readiness. Primary performance context is the Durham HPC evidence below, not
  hosted-runner timings.
- Caveat: the `onnx-runtime-manual` lane is **model-contract / systems** evidence only
  (loads the public-L2 ChronosLOB artefact and asserts deterministic scoring); it is
  not predictive-quality evidence.

## Determinism and replay

**Q: Where is fuzz-driven robustness testing?**
- Answer: [FUZZING.md](../FUZZING.md) documents five opt-in libFuzzer targets,
  their bounded seed corpora, build/run/reproduction commands and limitations.
- Files: `cpp/fuzz/`, `cpp/fuzz/corpus/`,
  [`.github/workflows/fuzz-smoke.yml`](../.github/workflows/fuzz-smoke.yml).
- Reproduce: configure Clang with `-DASTERION_BUILD_FUZZERS=ON
  -DASTERION_ENABLE_SANITIZERS=ON`, then run a target against its corpus as shown
  in `FUZZING.md`.
- Caveat: manual/opt-in robustness evidence only; not production-safety,
  real-exchange-correctness, live-trading, security-certification or performance
  evidence.

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
- Answer: [matching_semantics.md](matching_semantics.md) defines limit/market, IOC, FOK,
  post-only, replace priority, reject-vs-cancel, order states and STP interaction.
- Files: `tests/unit/test_matching_semantics.cpp`, `tests/golden/test_golden_traces.cpp`,
  `tests/property/test_book_properties.cpp`, `tests/unit/test_order_book.cpp`.

**Q: Is the matching engine cross-checked against an independent implementation?**
- Answer: yes — a small, independent Python reference matcher re-implements the same
  documented semantics from scratch, and a cross-check harness replays identical golden
  and fixed-seed random order flow into both the C++ `MatchingEngine` (through the Python
  bindings) and the reference, comparing the full execution-report sequence, the final L2
  book and the C++ canonical report checksum.
- Files: [docs/reference_matcher.md](reference_matcher.md),
  [`python/asterion/testing/reference_matcher.py`](../python/asterion/testing/reference_matcher.py),
  [`python/asterion/testing/cross_check.py`](../python/asterion/testing/cross_check.py),
  `python/tests/test_reference_matcher_golden.py`,
  `python/tests/test_reference_matcher_property.py`.
- Reproduce: `PYTHONPATH=build/python python -m pytest python/tests/test_reference_matcher_golden.py python/tests/test_reference_matcher_property.py`.
- Caveat: a test oracle / second specification for the documented contract; it does not
  prove production-exchange correctness, real-exchange completeness or live trading. L3
  FIFO is validated indirectly via the trade-report sequence (bindings expose aggregate
  L2, not a full per-order FIFO walk).

**Q: Where is the risk gateway / audit trail?**
- Files: `tests/unit/test_risk_gateway.cpp`, `test_risk_controls.cpp`, `test_risk_audit.cpp`; `python/tests/test_risk_tooling.py`. See [RISK.md](../RISK.md).
- Reproduce: `PYTHONPATH=build/python python scripts/asterion_inspect.py audit-summary --input data/samples/sample_risk_audit.jsonl --json`.

**Q: Where are audit manifests / tamper-evidence tested?**
- Files: `tests/unit/test_audit_manifest.cpp` (detects truncated/edited/missing/reordered files; HMAC RFC vector).
- Reproduce: `PYTHONPATH=build/python python scripts/asterion_inspect.py audit-manifest --input data/samples/sample_risk_audit.jsonl --output build/m.jsonl --json` then `audit-manifest-verify`.
- Caveat: signing is opt-in, local-key only; not managed retention/custody.

## Allocation and performance

**Q: Where is the primary performance evidence?**
- Answer: the **Durham Hamilton8 HPC** Slurm compute-node pass (Rocky Linux, GCC
  Release, one shared allocation) is the primary curated performance context.
- Report: [durham_hpc_performance_evaluation_2026_06_04.md](../reports/durham_hpc_performance_evaluation_2026_06_04.md).
- Which results were measured on Hamilton: GCC Release build/`ctest`, the 1M
  high-cancellation standard-vs-pooled hot path, eight small stress
  standard-vs-pooled corpora, six 1M steady-state SPSC corpora, the balanced-10k
  LinearModel replay-loop inference, and explicit `perf stat` counters +
  completed `perf record` hotspots for two targets.
- Which results remain laptop/WSL only: the Windows/MSYS2 ONNX ChronosLOB bridge,
  the optional inference event-loop ONNX row, the inference feature-buffer rows,
  the Binance public-depth case studies, and the original Windows/MSYS2 and WSL2
  hot-path/SPSC/inference passes (the latter is a virtualized-PMU laptop run).
- Was every old result recomputed on Hamilton? **No** — only the documented
  Hamilton paths above. The older laptop/WSL rows are retained verbatim as
  historical/local development evidence, attributed to their own environments.
  Cross-machine comparison is not meaningful.
- Caveat: Hamilton is a representative shared-HPC measurement (no LLC events, no
  root governor/turbo control, GCC only, ONNX not built, one inference hotspot
  OOM-killed); not portable, not production-HFT.

**Q: Where is the one-page summary of all performance/allocation evidence?**
- Answer: a top-level index of what the benchmarks prove and do not prove, with one cross-report
  evidence table, methodology and before/after summaries. It transcribes existing measured results
  only — no new numbers.
- Report: [performance_evidence_summary](../reports/performance_evidence_summary_2026_06_01.md).
- Caveat: representative local/environment measurements only; Linux perf evidence is now collected
  in WSL2 and on Durham Hamilton8 HPC, each with its own disclosed limits.

**Q: Where is the optimisation narrative (baseline → hotspot → pooled book → before/after)?**
- Answer: a measured performance-engineering case study that walks from the correctness-first
  node-based `OrderBook`, through the profiled hotspots, to the opt-in `PooledOrderBook` change, with
  before/after allocation and latency/throughput tables, checksum/parity evidence, the inference-path
  allocation split, the SPSC note and remaining bottlenecks — each table labelled by environment
  (Durham HPC / Win-MSYS2 / WSL2).
- Doc: [performance_deep_dive.md](performance_deep_dive.md). It transcribes existing measured results
  only; no new numbers.
- Caveat: representative measurements under disclosed conditions, not portable or production-HFT
  claims; the pooled book is opt-in and the correctness-first book remains the default.

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
- Fixture (hand-written): `data/models/chronoslob_tiny_fixture.onnx` (+ `.metadata.json`), `tools/export_chronoslob_tiny_onnx.py`, [docs/chronoslob_bridge.md](chronoslob_bridge.md).
- Synthetic-toy trained artefact: `data/models/chronoslob_tiny_real.onnx` (+ `.metadata.json`), exported by ChronosLOB `tools/export_tiny_asterion_onnx.py` from pushed commit `2cf2f32148bc38fb1009f1afaa5cb38deaf1f0b7`, a tiny `DeepLOBModel` trained on synthetic toy data (1×1×4→1×3).
- **Recorded-public-L2 model-contract artefact (current):** `data/models/chronoslob_public_l2_tiny.onnx` (+ `.metadata.json`, `.expected_input.json`, `.expected_output.json`, `.manifest.json`), source dataset `data/samples/binance_public_l2_window_sample.jsonl`, exported by ChronosLOB `tools/export_asterion_public_l2_onnx.py` from pushed commit `4e8fd562280385ebc713b7b8a13593728e3a10f6`. A windowed `DeepLOBModel` (`[1,16,40]→[1,3]`, window length 16, 40-dim LOB frame) trained on recorded public Binance crypto L2 depth, with normalisation metadata, source-data + model checksums and expected fixtures.
- Tests: `tests/unit/test_inference_backend.cpp` (ChronosLOB fixture + `[real]` + `[public_l2]` cases, including full 640-value metadata parsing, malformed/truncated array rejection, contract shapes, fallback, the isolated-benchmark config select-ONNX-or-detectably-skip guard and ONNX-lane fixture reproduction), `python/tests/test_chronoslob_bridge.py` (fixture + real metadata/sha256), `python/tests/test_chronoslob_public_l2_bridge.py` (windowed contract, normalisation, checksums, ONNX reproduction).
- Isolated C++ ONNX systems-cost rows (optional, ONNX-only): `public_l2_chronoslob_onnx_model_load` + `public_l2_chronoslob_onnx_inference_only` in `benchmarks/benchmark_main.cpp` reproduce `expected_test_output[0]` within `1e-3` before timing and are recorded as skipped (never timed as `LinearModel`) when ONNX Runtime is absent; the manual `onnx-runtime-manual` CI job smoke-tests the row's presence. Representative local: p50 ≈ 37 µs, p99 ≈ 109 µs, ≈ 23.2k inf/s, ~2 allocs/call (Win10/MSYS2 UCRT64 GCC, ONNX RT 1.20.1, not portable). Systems cost only — no predictive-quality claim.
- Reports: [chronoslob_onnx_bridge_report](../reports/chronoslob_onnx_bridge_report_2026_05_31.md) (fixture), [chronoslob_real_model_bridge_report](../reports/chronoslob_real_model_bridge_report_2026_06_01.md) (synthetic-toy model), [chronoslob_public_l2_model_bridge_report](../reports/chronoslob_public_l2_model_bridge_report_2026_06_04.md) (recorded-public-L2 model-contract artefact, incl. the isolated C++ ONNX row table).
- Caveat: fixture is deterministic and **not trained**; the synthetic-toy artefact **is** trained but on synthetic toy data (4-feature single-timestep); the recorded-public-L2 artefact **is** trained on recorded public crypto L2 depth with a windowed 40×16 contract but remains a systems/integration artefact — accuracy is diagnostic only, no predictive/profitability/alpha/live-trading/production/portable-latency/L3/equities claim; Asterion-side score is plumbing only.

**Q: What does it cost to put inference into the trading event loop?**
- Answer: per-event benchmark rows insert caller-owned feature extraction + model scoring + a measured timeout/late-signal policy gate into the replay hot path, measured against the inference-free hot path. The default `LinearModel` row remains dependency-light and added **0** steady-state allocations in the curated run. An optional ONNX Runtime row now measures the real tiny ChronosLOB backend inside the same replay-loop shape (**61.0 us p50 / 262.2 us p99**, 570,000 total allocations over 120k events in the opt-in local run); plumbing only, no predictive/profitability claim.
- Rows: `hot_path_binary_replay_l3_l2_inference_strategy_risk`; optional `hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk`; baseline `hot_path_binary_replay_l3_l2_strategy_risk`.
- Report: [inference_event_loop_cost_report](../reports/inference_event_loop_cost_report_2026_06_01.md). See also [BENCHMARKS.md](../BENCHMARKS.md).
- Caveat: representative local measurement on a tiny 12-event fixture; not portable; ONNX Runtime is optional and default builds report the ONNX replay row as skipped/unavailable.

## Shared multi-symbol replay parity

**Q: Where is grouped-vs-shared replay parity tested?**
- Answer: the opt-in shared multi-symbol path (`replay_shared_by_symbol`) is checked against the default grouped path (`replay_by_symbol`) on a hand-written golden matrix and a fixed-seed random matrix; `compare_replay_parity` returns a structured report and `describe_replay_parity` renders any mismatch.
- Covered: two/three-symbol interleaving, snapshot begin/end, cancel-after-interleave, replace-heavy, per-symbol sequence gaps, timestamp reversals, invalid events, both order-id duplicate cases (order ids are per-symbol), fixed-seed random corpora, deterministic repeat runs.
- Files: `tests/unit/test_shared_replay_parity.cpp`, `tests/unit/test_multi_symbol.cpp`; `python/tests/test_shared_replay_parity.py`, `python/tests/test_replay_stability.py`.
- Contract: [docs/shared_replay_parity.md](shared_replay_parity.md).
- Reproduce: `./build/asterion_tests "[parity]"`; `PYTHONPATH=build/python python -m pytest python/tests/test_shared_replay_parity.py`.
- Caveat: grouped replay is the default; shared replay is opt-in; parity coverage is stronger for tested cases, not exhaustively proven for all workloads; not a cross-symbol matching engine.

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
- Tests: `python/tests/test_binance_normalise.py`. Reports: [binance_replay_case_study](../reports/binance_replay_case_study_2026_05_31.md), [binance_larger_replay_case_study](../reports/binance_larger_replay_case_study_2026_06_01.md).
- Reproduce: normalise the fixture to CSV/binary, then `./build/asterion_replay --input build/binance_sample.bin --format binary` (see README "Recorded Binance Public Depth Case Study").
- Caveat: recorded public depth demo; not live trading, not authenticated, not equities-market realism.

**Q: Where is the larger recorded public crypto L2 replay evidence?**
- Files: `data/samples/binance_depth_larger_sample.raw.jsonl`, `data/samples/binance_depth_larger_sample.expected.json`, `data/samples/binance_depth_larger_sample.normalised.csv`, `data/samples/binance_depth_larger_sample.normalised.bin`.
- Tests: `python/tests/test_binance_normalise.py::test_larger_fixture_regeneration_guard_matches_expected_manifest`, plus the parametrized CSV/binary replay and grouped/shared parity tests in the same file.
- Report: [binance_larger_replay_case_study](../reports/binance_larger_replay_case_study_2026_06_01.md).
- Reproduce: `PYTHONPATH=build/python python -m pytest python/tests/test_binance_normalise.py -v`.
- Caveat: recorded public Binance crypto L2 snapshots with deterministic synthetic order IDs; no L3/equities/live/authenticated/profitability/production claim.

## Scope and limitations

**Q: Where are limitations stated?**
- File: [LIMITATIONS.md](../LIMITATIONS.md) (full scope statement); per-component notes also live at definition sites and in [claim_audit.md](claim_audit.md).

**Q: What is the Linux perf status?**
- Answer: **collected in WSL2 (2026-06-01) and Durham Hamilton8 HPC (2026-06-04).** After the BIOS/UEFI firmware-virtualization
  blocker was enabled, WSL2 boots a Linux kernel, the project builds and `ctest` passes,
  and the virtualized PMU exposes hardware counters. `perf stat -d` (cycles/instructions/
  IPC/branch/cache) and `perf record` hotspots were captured around the steady-state
  replay and hot-path workloads, alongside a 1M standard-vs-pooled hot path, 1M SPSC
  steady-state and a LinearModel inference replay-loop comparison.
- Durham added a non-WSL Slurm compute-node pass: GCC Release build/test, explicit
  `perf stat` counters, completed hotspots for two targets, 1M hot-path evidence,
  six 1M SPSC rows and balanced-10k LinearModel replay-loop inference evidence.
- Environment: WSL2 (Microsoft Hyper-V), Ubuntu 24.04.4, kernel
  `6.6.114.1-microsoft-standard-WSL2`, GCC 13.3.0 Release, `perf 6.8.12`. The
  virtualized PMU reports `LLC` cache events as `<not supported>` and multiplexes
  counters; CPU turbo is uncontrolled. Representative WSL2 measurements only — not
  native/cloud Linux, not portable.
- Environment: Durham Hamilton8 HPC, Rocky Linux 8.10, kernel
  `4.18.0-553.123.1.el8_10.x86_64`, AMD EPYC 7702, GCC 13.2 Release, Slurm job
  `17356789` on `cn025.ham8.dur.ac.uk`. Explicit `perf stat` runs counted
  cycles/instructions/branch/cache/L1 events; `LLC` events were unsupported and
  governor/turbo were not controllable from the non-root allocation. One inference
  hotspot report was OOM-killed; JSON/counter files and two other hotspot reports
  were preserved.
- Files: [reports/linux_performance_evaluation_2026_06_01.md](../reports/linux_performance_evaluation_2026_06_01.md),
  [reports/durham_hpc_performance_evaluation_2026_06_04.md](../reports/durham_hpc_performance_evaluation_2026_06_04.md),
  [reports/perf_profile.md](../reports/perf_profile.md), `scripts/run_linux_perf_profile.sh`.
- Caveat: both runs are representative environment-specific measurements, not portable
  claims. A more controlled bare-metal/cloud pass would still help if it exposes LLC
  events, lets frequency be fixed and uses a profiling build for flamegraph-quality
  call graphs. Counter values are never fabricated when a real PMU is unavailable.

## Toolchain note (Windows)

**Q: Why does pytest fail to import `asterion` after a successful build?**
- Answer: the compiled `_native` extension is ABI-specific to the Python that built it.
  An MSYS2/UCRT64 build produces a `cp3XX-mingw` `.pyd` that a different system Python
  cannot load. Build and run tests/demo with the **same** interpreter (pass
  `-PythonExe` to the PowerShell scripts to pin it). Default Linux CI uses one
  interpreter throughout and is unaffected.
