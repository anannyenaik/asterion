# Reports Index

These reports are **representative local measurements on a single machine/environment,
not portable performance claims.** No benchmark JSON is committed; every report exists
to document methodology and one set of local results so a reviewer can reproduce the
shape of the result, not the exact numbers.

For how each report maps to a claim, see [docs/claim_audit.md](../docs/claim_audit.md);
for reproduction commands, see [docs/evidence_index.md](../docs/evidence_index.md).

| Report | What it covers | Results | Optional deps | Key limitation |
| --- | --- | --- | --- | --- |
| [benchmark_report_2026_05_31.md](benchmark_report_2026_05_31.md) | Hot-path benchmark: binary replay → L3 update → reusable L2 → strategy → risk; core + inference benchmark categories | Local representative | None | Machine-dependent; not portable; not production-HFT |
| [allocation_optimisation_report_2026_05_31.md](allocation_optimisation_report_2026_05_31.md) | Before/after allocation comparison for the opt-in pooled L3 path | Local representative | None (pooled path is opt-in) | Zero-alloc only after warm-up in disclosed paths; opt-in |
| [pooled_order_book_stress_report_2026_05_31.md](pooled_order_book_stress_report_2026_05_31.md) | PooledOrderBook under generated stress corpora; parity vs correctness-first book | Local representative | None | Opt-in; single-symbol at book layer; not production-HFT |
| [inference_report_2026_05_31.md](inference_report_2026_05_31.md) | Inference plumbing cost: feature extraction, LinearModel scoring, policy gate | Local representative | None | Plumbing only; no predictive/profitability claim; sub-µs timings dominated by timer resolution |
| [inference_feature_buffer_report_2026_05_31.md](inference_feature_buffer_report_2026_05_31.md) | Caller-owned zero-allocation feature buffer path | Local representative | None | Vector-returning path still allocates; alloc-free claim is scoped + warmed |
| [chronoslob_onnx_bridge_report_2026_05_31.md](chronoslob_onnx_bridge_report_2026_05_31.md) | Research-model → systems integration via tiny ChronosLOB-style ONNX fixture | Local representative | **ONNX Runtime** for the real-backend rows | Fixture is deterministic, not trained; no predictive quality; not production model serving |
| [chronoslob_real_model_bridge_report_2026_06_01.md](chronoslob_real_model_bridge_report_2026_06_01.md) | Real tiny ChronosLOB `DeepLOBModel` (trained on synthetic toy data) exported to ONNX and run through the optional backend | Local representative | **ONNX Runtime** for the real-model rows | Trained on synthetic toy data only; reduced 4-feature single-timestep; no predictive/profitability/live-trading/production claim; latency/alloc numbers local, not portable |
| [binance_replay_case_study_2026_05_31.md](binance_replay_case_study_2026_05_31.md) | Recorded public Binance depth → normalise → deterministic replay | Recorded-data demo | None (capture is manual/opt-in) | Public depth is L2 → synthetic order IDs; not live trading; not equities-market realism |
| [spsc_replay_pipeline_report_2026_05_31.md](spsc_replay_pipeline_report_2026_05_31.md) | Opt-in bounded SPSC replay pipeline; checksum parity with single-thread | Local representative | None | Opt-in concurrency boundary; not production networking or a latency guarantee |
| [spsc_steady_state_report_2026_05_31.md](spsc_steady_state_report_2026_05_31.md) | Steady-state SPSC throughput with `ReplayValidationMode::Light` on large corpora | Local representative | None (corpora git-ignored) | `Light` is opt-in; `Full` validation remains the default correctness path |
| [linux_performance_evaluation_2026_05_31.md](linux_performance_evaluation_2026_05_31.md) | Larger-corpus standard-vs-pooled evaluation + ready-to-run Linux `perf` helper | Local representative; **`perf` counters pending native Linux/WSL** | None | Counter values not fabricated when `perf` is unavailable |
| [perf_profile.md](perf_profile.md) | Linux `perf` profiling status / methodology placeholder | Pending native Linux/WSL | None | No counter values captured in the current environment |

## Subdirectories

- `latency_histograms/` — supporting latency-histogram artefacts referenced by the inference/latency reports.
