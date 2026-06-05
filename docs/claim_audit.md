# Claim Audit

This document audits every major claim Asterion makes and classifies the evidence
behind it. It is the single place a reviewer can use to check that nothing in the
README, [DESIGN.md](../DESIGN.md), [BENCHMARKS.md](../BENCHMARKS.md) or the reports
overclaims.

Asterion is **a deterministic C++20 trading systems lab for replay, matching, risk,
inference integration, benchmarking and correctness evaluation.** It is not a real
exchange, an HFT bot, live trading infrastructure, production model serving, or
evidence of profitability or equities-market realism.

## Classification legend

| Tag | Meaning |
| --- | --- |
| **Implemented + tested** | Code exists and is covered by automated C++ and/or Python tests run in default CI. |
| **Implemented + benchmarked (local)** | Code exists and has representative local measurements in a `reports/` file. Numbers are machine-dependent, not portable. |
| **Implemented but optional** | Code exists behind an opt-in flag/API; the default path does not use it. |
| **Simulated only** | In-process deterministic model; never touches a network or live venue. |
| **Recorded-data demo only** | Works on checked-in/recorded fixtures; no live connectivity. |
| **Future work** | Not implemented; listed to mark the boundary. |
| **Explicitly not claimed** | Deliberately out of scope; called out so it is never inferred. |

Evidence shorthand: C++ tests live in [`tests/unit/`](../tests/unit), Python tests in
[`python/tests/`](../python/tests), reports in [`reports/`](../reports). See
[evidence_index.md](evidence_index.md) for exact commands.

## Core claims

| Claim | Classification | Primary evidence |
| --- | --- | --- |
| Deterministic replay (stable book / execution-report / diagnostics / event-log checksums) | Implemented + tested | `tests/unit/test_event_log_replay.cpp` ("Replay checksums are stable…", "Replay engine validates sequence and final checksum deterministically"); `python/tests/test_replay_stability.py`; demo reproduces checksums |
| CSV / binary event-log schema stability (v1 wire contract) | Implemented + tested | `tests/unit/test_event_log_replay.cpp` ("Event-log schema constants match the documented v1 wire contract", "Binary event-log writer preserves v1 header and record field layout"); `python/tests/test_event_log_schema.py`; [`data/schema/event_log_schema_v1.json`](../data/schema/event_log_schema_v1.json); [docs/event_log_schema.md](event_log_schema.md) |
| Malformed / truncated input is rejected safely | Implemented + tested | `tests/unit/test_event_log_replay.cpp` ("Malformed CSV/binary event logs are rejected safely") |
| L3 order book (order-ID lookup, FIFO per level, invariants, checksum) | Implemented + tested | `tests/unit/test_order_book.cpp` ("L3 order book preserves FIFO and aggregates L2 levels", "Order book rejects malformed and adversarial operations") |
| Price-time-priority matching (limit/market/cancel/replace, IOC/FOK/post-only, partial/full fills, STP backstop) | Implemented + tested | [`matching_semantics.md`](matching_semantics.md); `tests/unit/test_matching_semantics.cpp`; golden + randomized cases |
| Execution reports with deterministic report checksums | Implemented + tested | `tests/unit/test_event_log_replay.cpp`; demo prints report checksum |
| Snapshot reconstruction with deterministic checksums | Implemented + tested | `tests/unit/test_snapshot.cpp` |
| Pre-trade risk gateway (qty, notional, position, exposure, price-band, stale-data, dup-ID, kill-switch) | Implemented + tested | `tests/unit/test_risk_gateway.cpp`, `tests/unit/test_risk_controls.cpp` |
| Opt-in advanced risk controls (working exposure, rate limiting, self-trade prevention, replace rechecks, cancel-on-disconnect/kill) | Implemented but optional | `tests/unit/test_risk_controls.cpp`; disabled by default ("Default gateway leaves the new controls disabled") |
| Risk audit trail + persistent append-only logs + deterministic audit checksum | Implemented + tested | `tests/unit/test_risk_audit.cpp`; `python/tests/test_risk_tooling.py` |
| Tamper-evident audit manifests with optional HMAC-SHA256 signing | Implemented + tested | `tests/unit/test_audit_manifest.cpp` ("…detects a truncated/edited/missing file", "HMAC-SHA256 matches the known RFC test vector"); not a retention/custody system |
| Simulated broker/session lifecycle | Simulated only | `tests/unit/test_broker_session.cpp`; never sends network messages |
| Simulated portfolio-risk accounting gate | Simulated only | `tests/unit/test_portfolio_risk.cpp` |

## CI verification claims

| Claim | Classification | Primary evidence |
| --- | --- | --- |
| Default CI builds + tests on GCC and Clang (C++20 Release, strict warnings) | Implemented + tested in default CI | [.github/workflows/ci.yml](../.github/workflows/ci.yml) jobs `gcc-release`, `clang-release` |
| Python bindings, inspection CLI and one-command demo pass on a single interpreter | Implemented + tested in default CI | `ci.yml` job `python-bindings` |
| Tested C++ paths are clean under Address + UndefinedBehaviour sanitizers | Implemented + tested in default CI | [.github/workflows/sanitizers.yml](../.github/workflows/sanitizers.yml) job `asan-ubsan` (Debug, `-DASTERION_ENABLE_SANITIZERS=ON`, unit/golden/property suite including the full public-L2 metadata/model-contract fixture). Large numeric arrays use bounded iterative parsing, so the lane gates every push/PR without a raised stack limit. Correctness/memory-UB evidence only; not performance or production-readiness evidence. |
| Real ONNX Runtime backend loads checked-in ChronosLOB artefacts (fixture, real tiny model, public-L2 model contract) and scores deterministically | Implemented but optional + **manual CI only** | `ci.yml` job `onnx-runtime-manual` (dispatch `onnx_backend=true`). Model-contract / systems evidence; **not** predictive-quality evidence. ONNX Runtime never installed by default CI. |
| Isolated C++ ONNX systems-cost row for the recorded-public-L2 `[1,16,40]` artefact (`public_l2_chronoslob_onnx_inference_only`) | Implemented but optional + **manual/ONNX-only** | `benchmarks/benchmark_main.cpp`; reproduces the recorded expected output within `1e-3` before timing, recorded as skipped (never timed as `LinearModel`) when ONNX Runtime is absent, smoke-tested in `ci.yml` job `onnx-runtime-manual`. Representative local timing only (not portable); **systems cost, not** predictive-quality, alpha or production-serving evidence. |
| Benchmark / latency numbers as a CI pass-fail gate | **Explicitly not claimed** | `benchmarks` + `linux-performance` are manual `workflow_dispatch` only; comparisons run without `--fail-on-regression`. Numbers are reported, not gated. |
| CI proves performance, predictive quality or production readiness | **Explicitly not claimed** | CI proves build/correctness/portability/sanitizer-backed tested-path safety and optional-inference plumbing only; performance context is the Durham HPC reports below |

## Performance / allocation claims

| Claim | Classification | Primary evidence |
| --- | --- | --- |
| Hot-path benchmark (binary replay → L3 → reusable L2 → strategy → risk), stable JSON, alloc counters | Implemented + benchmarked (local) | `benchmarks/benchmark_main.cpp`; [reports/benchmark_report_2026_05_31.md](../reports/benchmark_report_2026_05_31.md) |
| Reusable L2 view / fixed strategy callback / reserved risk sub-paths are allocation-free after warm-up | Implemented + tested | `tests/unit/test_allocation_tracking.cpp`, `test_hot_path.cpp` (scoped, warmed) |
| Caller-owned zero-allocation feature buffer path | Implemented + tested | `tests/unit/test_telemetry_inference.cpp` ("Caller-owned feature extraction and LinearModel scoring do not allocate after warm-up"); [reports/inference_feature_buffer_report_2026_05_31.md](../reports/inference_feature_buffer_report_2026_05_31.md). The vector-returning convenience path still allocates one vector/call. |
| PooledOrderBook allocation reduction after warm-up | Implemented but optional + benchmarked (local) | `tests/unit/test_pooled_order_book.cpp` ("…allocation-free after explicit warm-up", "matches stable book…"); [reports/allocation_optimisation_report_2026_05_31.md](../reports/allocation_optimisation_report_2026_05_31.md), [reports/pooled_order_book_stress_report_2026_05_31.md](../reports/pooled_order_book_stress_report_2026_05_31.md). Opt-in; correctness-first `OrderBook` is the default. |
| Per-stage latency-budget accounting (deterministic config checksum, machine-dependent ns) | Implemented + tested | `tests/unit/test_latency_budget.cpp`; demo emits latency JSON |
| Larger-corpus standard-vs-pooled evaluation + Linux perf helper | Implemented but optional + benchmarked (local) | [reports/linux_performance_evaluation_2026_05_31.md](../reports/linux_performance_evaluation_2026_05_31.md) (Windows/MSYS2); [reports/linux_performance_evaluation_2026_06_01.md](../reports/linux_performance_evaluation_2026_06_01.md) (WSL2 Linux + perf counters); corpora are git-ignored |
| **Primary** Durham Hamilton8 HPC Linux `perf` counter + latency-distribution evidence (GCC build/test, cycles/IPC/branch/cache/L1, hotspots, std-vs-pooled, SPSC, inference-loop) | Implemented + benchmarked (HPC compute node) | [reports/durham_hpc_performance_evaluation_2026_06_04.md](../reports/durham_hpc_performance_evaluation_2026_06_04.md), [reports/perf_profile.md](../reports/perf_profile.md). **Primary performance context.** One shared Slurm compute-node allocation, GCC evidence only, no LLC events, no root governor/turbo control, one inference hotspot report OOM-killed - representative shared-HPC measurement, not portable, not production-HFT. Supersedes the laptop/WSL context only for the paths it measured. |
| Historical / local development WSL2 Linux `perf` counter + latency-distribution evidence (cycles/IPC/branch/cache, hotspots, std-vs-pooled, SPSC, inference-loop) | Implemented + benchmarked (local, historical) | [reports/linux_performance_evaluation_2026_06_01.md](../reports/linux_performance_evaluation_2026_06_01.md), [reports/perf_profile.md](../reports/perf_profile.md). **WSL2, one laptop, virtualized PMU (no LLC events, multiplexed counters), uncontrolled turbo** — retained as a local development baseline, not native/cloud Linux, not portable, not production-HFT. |
| Top-level performance evidence summary (cross-report index; no new numbers) | Implemented + benchmarked (local) | [reports/performance_evidence_summary_2026_06_01.md](../reports/performance_evidence_summary_2026_06_01.md); transcribes existing reports only, preserves all claim boundaries |

## Concurrency claims (SPSC)

| Claim | Classification | Primary evidence |
| --- | --- | --- |
| Opt-in bounded SPSC replay pipeline; consumer reuses single-thread `ReplayEngine` so checksums are bit-identical regardless of thread timing | Implemented but optional + tested | `tests/unit/test_spsc_replay.cpp`, `test_spsc_ring_buffer.cpp`; `python/tests/test_spsc_replay.py`; [reports/spsc_replay_pipeline_report_2026_05_31.md](../reports/spsc_replay_pipeline_report_2026_05_31.md) |
| Lossless blocking backpressure by default; opt-in `DropNewestOnFull` for overload-shedding only (not correctness-preserving) | Implemented but optional + tested | `tests/unit/test_spsc_replay.cpp` ("…tiny queue forces backpressure but stays lossless", "opt-in drop policy sheds events under overload") |
| End-of-stream delivered exactly once; max queue depth never exceeds capacity; zero-capacity rejected without deadlock | Implemented + tested | `tests/unit/test_spsc_replay.cpp` / `test_spsc_ring_buffer.cpp` |
| Steady-state SPSC throughput harness + `ReplayValidationMode::Light` | Implemented but optional + benchmarked (local) | `tests/unit/test_spsc_replay.cpp` (steady-state cases); [reports/spsc_steady_state_report_2026_05_31.md](../reports/spsc_steady_state_report_2026_05_31.md). `Light` keeps cheap per-event checks + full validation at end-of-replay; `Full` remains the default. |
| Pooled-book SPSC variant | Future work / not implemented | `ReplayEngine` is not templated on book type; documented in [LIMITATIONS.md](../LIMITATIONS.md) |

## Inference / ML-integration claims

| Claim | Classification | Primary evidence |
| --- | --- | --- |
| Deterministic `LinearModel` backend, measured latency accounting, policy gate | Implemented + tested | `tests/unit/test_inference_backend.cpp`, `test_telemetry_inference.cpp` |
| Explicit backend selection (`make_inference_backend`) | Implemented + tested | `tests/unit/test_inference_backend.cpp` ("Linear backend is selected and scores deterministically") |
| Optional ONNX Runtime backend behind `ASTERION_USE_ONNXRUNTIME`; deterministic fallback to `LinearModel` when absent | Implemented but optional | `tests/unit/test_inference_backend.cpp` ("ONNX request falls back to LinearModel when ONNX Runtime is absent", "ONNX Runtime fixture backend scores deterministically when opt-in dependency is present"). Not in default CI. |
| ChronosLOB-style ONNX fixture bridge (tiny 1×4→1×1 fixture, metadata, regen tool) | Recorded-data demo only / Implemented but optional | `tests/unit/test_inference_backend.cpp` (ChronosLOB cases); `python/tests/test_chronoslob_bridge.py`; [docs/chronoslob_bridge.md](chronoslob_bridge.md); [reports/chronoslob_onnx_bridge_report_2026_05_31.md](../reports/chronoslob_onnx_bridge_report_2026_05_31.md). Fixture is deterministic, **not trained**. |
| Real tiny ChronosLOB `DeepLOBModel` exported to ONNX (1×1×4→1×3), trained on synthetic toy data; systems-integration / inference-latency only | Implemented but optional | `tests/unit/test_inference_backend.cpp` (`[real]` cases, incl. ONNX-lane load+deterministic score); `python/tests/test_chronoslob_bridge.py` (real metadata + sha256); [reports/chronoslob_real_model_bridge_report_2026_06_01.md](../reports/chronoslob_real_model_bridge_report_2026_06_01.md). **Trained** but on synthetic toy data; **no** predictive/profitability/live-trading/production claim. |
| Recorded-public-L2 ChronosLOB `DeepLOBModel` model-contract artefact (windowed 1×16×40→1×3, window length 16, 40-dim LOB frame), trained on recorded public Binance crypto L2 depth; normalisation metadata + source-data/model checksums + expected fixtures | Recorded-data demo only / Implemented but optional | `tests/unit/test_inference_backend.cpp` (`[public_l2]`/`[parser]` cases: bounded full-fixture metadata parsing, malformed/truncated array rejection, contract-shape and expected-length checks, not-the-live-contract fallback, ONNX-lane load + fixture reproduction); `python/tests/test_chronoslob_public_l2_bridge.py` (contract, normalisation, checksums, ONNX reproduction); ChronosLOB `tools/export_asterion_public_l2_onnx.py` @ `4e8fd56`; [reports/chronoslob_public_l2_model_bridge_report_2026_06_04.md](../reports/chronoslob_public_l2_model_bridge_report_2026_06_04.md). **Trained on recorded public crypto L2 depth** but a systems/integration artefact only — accuracy is diagnostic; **no** predictive/profitability/alpha/live-trading/production/portable-latency/L3/equities claim; not Asterion's live 4-feature contract (falls back, cannot masquerade). |
| ONNX inference allocations measured honestly (load separated from steady-state, not claimed alloc-free) | Implemented + tested | `tests/unit/test_inference_backend.cpp` ("ONNX inference allocations are measured honestly and separated from load", real-model load/steady-state case) |
| Timeout/late-signal policy can disable model after repeated late signals when configured | Implemented but optional | `tests/unit/test_telemetry_inference.cpp` (policy-gate cases); disabled by default |
| Event-loop inference cost: replay → L3 → L2 → caller-owned feature extraction → model scoring → measured policy gate, alongside strategy + risk, measured vs the inference-free hot path | Implemented + benchmarked (local); ONNX path optional | `benchmarks/benchmark_main.cpp` (`hot_path_binary_replay_l3_l2_inference_strategy_risk`, optional `hot_path_binary_replay_l3_l2_chronoslob_real_onnx_inference_strategy_risk`); [reports/inference_event_loop_cost_report_2026_06_01.md](../reports/inference_event_loop_cost_report_2026_06_01.md). LinearModel stage adds **0** steady-state allocations on top of the node-based book; optional ONNX row is measured only when active ONNX is available and is not allocation-free; plumbing only, no decisioning/alpha/profitability claim; tiny 12-event fixture, local only |
| Predictive quality / signal value / alpha / profitability | **Explicitly not claimed** | Inference path measures plumbing only; stated in every inference report header |

## Market-data claims

| Claim | Classification | Primary evidence |
| --- | --- | --- |
| Binance recorded public-depth case study (normalise → replay) | Recorded-data demo only | `python/tests/test_binance_normalise.py`; `tools/normalise_binance_depth_to_asterion.py`; [docs/market_data.md](market_data.md); [reports/binance_replay_case_study_2026_05_31.md](../reports/binance_replay_case_study_2026_05_31.md). Tiny hand-curated fixture; capture is manual/opt-in, never in CI. |
| Larger recorded public crypto L2 replay case study (public REST depth snapshots -> normalise -> CSV/binary -> deterministic replay -> diagnostics/checksums -> grouped/shared parity) | Recorded-data demo only | `data/samples/binance_depth_larger_sample.*`; `data/samples/binance_depth_larger_sample.expected.json`; `python/tests/test_binance_normalise.py::test_larger_fixture_regeneration_guard_matches_expected_manifest`; [reports/binance_larger_replay_case_study_2026_06_01.md](../reports/binance_larger_replay_case_study_2026_06_01.md). Public Binance crypto L2 snapshots only; no live/authenticated connectivity, L3/equities realism, profitability, predictive-quality or production claim. |
| Live capture is public REST `/api/v3/depth` only, no keys, no order placement | Recorded-data demo only | `tools/capture_binance_depth.py`; `test_capture_module_imports_without_network` |
| Real L3 order identity / per-level FIFO depth / true order lifetimes from Binance | **Explicitly not claimed** | Binance depth is L2; normaliser uses synthetic order IDs + level-replacement (stated in LIMITATIONS) |
| Equities-market realism | **Explicitly not claimed** | Stated in README + case-study header |
| Opt-in shared multi-symbol replay (`MultiSymbolBookSet`) with grouped-vs-shared parity | Implemented but optional + tested | `tests/unit/test_multi_symbol.cpp`, `python/tests/test_replay_stability.py`; grouped replay is the default; not a cross-symbol matching engine |

## Boundary claims (deliberately out of scope)

| Claim | Classification |
| --- | --- |
| Live exchange / broker / market-data connectivity | **Explicitly not claimed** |
| Order placement / authenticated exchange connectivity | **Explicitly not claimed** |
| Production-HFT performance, kernel bypass, FPGA, colocated networking | **Explicitly not claimed** |
| Profitability / alpha / signal value | **Explicitly not claimed** |
| Portable / committed benchmark or latency numbers | **Explicitly not claimed** (all numbers are representative local measurements) |
| Managed audit retention, custody, compliance, tamper-proof storage | **Explicitly not claimed** |
| Cross-symbol / portfolio-level matching | Future work |
| Multi-version schema migration framework | Future work (v1 is stable + guarded) |
| Current Linux `perf` counter evidence | Implemented + benchmarked (local) - **collected 2026-06-01 in WSL2** and **2026-06-04 on Durham Hamilton8 HPC**. WSL2 provides virtualized PMU evidence on one laptop; Durham provides one Slurm compute-node GCC/Rocky Linux run. Both are representative environment-specific measurements, not portable. See [reports/linux_performance_evaluation_2026_06_01.md](../reports/linux_performance_evaluation_2026_06_01.md), [reports/durham_hpc_performance_evaluation_2026_06_04.md](../reports/durham_hpc_performance_evaluation_2026_06_04.md) and [reports/perf_profile.md](../reports/perf_profile.md). |
| More controlled Linux `perf` (LLC events when available, fixed governor/turbo, frame-pointer flamegraphs) | Future work - the WSL2 run has a virtualized PMU; the Durham run has no LLC events and no root governor/turbo control. A more controlled host/allocation would improve counter completeness and call-graph quality. |

## Auditor's note

No overclaim was found that required rewriting during this audit pass. Every numeric
result in `reports/` carries a "representative local measurements, not portable
performance claims" header, and every simulated/optional component is labelled at its
definition site and in [LIMITATIONS.md](../LIMITATIONS.md). This document and
[evidence_index.md](evidence_index.md) were added to make that mapping explicit for a
10-minute review.
