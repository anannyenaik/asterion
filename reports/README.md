# Reports Index

These reports are **representative local measurements on a single machine/environment,
not portable performance claims.** No benchmark JSON is committed; every report exists
to document methodology and one set of local results so a reviewer can reproduce the
shape of the result, not the exact numbers.

For how each report maps to a claim, see [docs/claim_audit.md](../docs/claim_audit.md);
for reproduction commands, see [docs/evidence_index.md](../docs/evidence_index.md).
For reviewer navigation before reading individual reports, see the
[architecture overview](../docs/architecture_overview.md), the
[reproducible development environment](../docs/reproducible_dev_environment.md)
and the compact benchmark evidence table near the top of the
[README](../README.md#representative-benchmark-evidence).

**Primary performance context:** the curated performance report is now the
**Durham Hamilton8 HPC** Slurm compute-node pass
([durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md)).
The Windows/MSYS2 Lenovo-laptop and WSL2-laptop reports are retained as
**historical / local development** baselines. Durham supersedes the laptop/WSL
context only for the paths it actually measured; not every old result was re-run
on Hamilton, and cross-machine comparison is not meaningful.

| Report | What it covers | Results | Optional deps | Key limitation |
| --- | --- | --- | --- | --- |
| [performance_evidence_summary_2026_06_01.md](performance_evidence_summary_2026_06_01.md) | **Top-level summary**: what the benchmark/allocation evidence proves and does not prove, one cross-report evidence table, methodology, before/after | Local representative (index of existing reports) | **ONNX Runtime** only for the ChronosLOB row | Transcribes curated reports; leads with Durham HPC as the primary performance context, laptop/WSL kept as historical; not portable |
| [benchmark_report_2026_05_31.md](benchmark_report_2026_05_31.md) | Hot-path benchmark: binary replay → L3 update → reusable L2 → strategy → risk; core + inference benchmark categories | Local representative | None | Machine-dependent; not portable; not production-HFT |
| [allocation_optimisation_report_2026_05_31.md](allocation_optimisation_report_2026_05_31.md) | Before/after allocation comparison for the opt-in pooled L3 path | Local representative | None (pooled path is opt-in) | Zero-alloc only after warm-up in disclosed paths; opt-in |
| [pooled_order_book_stress_report_2026_05_31.md](pooled_order_book_stress_report_2026_05_31.md) | PooledOrderBook under generated stress corpora; parity vs correctness-first book | Local representative | None | Opt-in; single-symbol at book layer; not production-HFT |
| [inference_report_2026_05_31.md](inference_report_2026_05_31.md) | Inference plumbing cost: feature extraction, LinearModel scoring, policy gate | Local representative | None | Plumbing only; no predictive/profitability claim; sub-µs timings dominated by timer resolution |
| [inference_feature_buffer_report_2026_05_31.md](inference_feature_buffer_report_2026_05_31.md) | Caller-owned zero-allocation feature buffer path | Local representative | None | Vector-returning path still allocates; alloc-free claim is scoped + warmed |
| [inference_event_loop_cost_report_2026_06_01.md](inference_event_loop_cost_report_2026_06_01.md) | Systems cost of inserting inference into the deterministic replay event loop: LinearModel row plus optional real ChronosLOB ONNX replay-loop row | Local representative (default + opt-in ONNX runs) | **ONNX Runtime** only for optional ONNX rows; default builds report the ONNX replay row as skipped/unavailable | Plumbing only; tiny 12-event fixture; ONNX row is not allocation-free; no predictive/profitability claim; not portable |
| [chronoslob_onnx_bridge_report_2026_05_31.md](chronoslob_onnx_bridge_report_2026_05_31.md) | Research-model → systems integration via tiny ChronosLOB-style ONNX fixture | Local representative | **ONNX Runtime** for the real-backend rows | Fixture is deterministic, not trained; no predictive quality; not production model serving |
| [chronoslob_real_model_bridge_report_2026_06_01.md](chronoslob_real_model_bridge_report_2026_06_01.md) | Real tiny ChronosLOB `DeepLOBModel` (trained on synthetic toy data) exported to ONNX and run through the optional backend | Local representative | **ONNX Runtime** for the real-model rows | Trained on synthetic toy data only; reduced 4-feature single-timestep; no predictive/profitability/live-trading/production claim; latency/alloc numbers local, not portable |
| [chronoslob_public_l2_model_bridge_report_2026_06_04.md](chronoslob_public_l2_model_bridge_report_2026_06_04.md) | Recorded-public-L2 ChronosLOB `DeepLOBModel` (windowed `[1,16,40]→[1,3]`) trained on recorded public Binance crypto L2 depth; model-contract validation + isolated ONNX latency diagnostic | Local representative | **ONNX Runtime** only for the ONNX-lane reproduction + latency rows | Recorded public crypto L2 only (no L3/equities); window length 16; trained on a tiny overlapping window set; accuracy is diagnostic only — no predictive/profitability/alpha/live-trading/production/portable-latency claim |
| [binance_replay_case_study_2026_05_31.md](binance_replay_case_study_2026_05_31.md) | Recorded public Binance depth → normalise → deterministic replay | Recorded-data demo | None (capture is manual/opt-in) | Public depth is L2 → synthetic order IDs; not live trading; not equities-market realism |
| [binance_larger_replay_case_study_2026_06_01.md](binance_larger_replay_case_study_2026_06_01.md) | Larger recorded public Binance depth fixture -> normalise -> CSV/binary -> deterministic replay -> diagnostics/checksums -> grouped/shared parity | Recorded-data correctness demo | None (capture was manual/opt-in; CI is fixture-only) | Public crypto depth is L2 snapshot data -> synthetic order IDs; no live/authenticated connectivity; no L3/equities/profitability/production claim |
| [spsc_replay_pipeline_report_2026_05_31.md](spsc_replay_pipeline_report_2026_05_31.md) | Opt-in bounded SPSC replay pipeline; checksum parity with single-thread | Local representative | None | Opt-in concurrency boundary; not production networking or a latency guarantee |
| [spsc_steady_state_report_2026_05_31.md](spsc_steady_state_report_2026_05_31.md) | Steady-state SPSC throughput with `ReplayValidationMode::Light` on large corpora | Local representative | None (corpora git-ignored) | `Light` is opt-in; `Full` validation remains the default correctness path |
| [durham_hpc_performance_evaluation_2026_06_04.md](durham_hpc_performance_evaluation_2026_06_04.md) | **PRIMARY performance report — Durham Hamilton8 HPC** Slurm compute-node perf-counter + latency-distribution evidence: GCC Release build/test, 1M hot path, SPSC steady-state, inference replay-loop, `perf stat` counters and completed `perf record` hotspots | Representative HPC compute-node measurement | None (ONNX not built; ONNX replay row skipped) | Single shared Slurm allocation; GCC evidence only; no LLC events; governor/turbo uncontrolled; one inference `perf report` killed by OOM; not portable |
| [linux_performance_evaluation_2026_06_01.md](linux_performance_evaluation_2026_06_01.md) | **Historical / local development (WSL2 laptop)** perf-counter + latency-distribution evidence: 1M SPSC steady-state, 1M std-vs-pooled hot path, inference replay-loop, `perf stat -d` counters + `perf record` hotspots | Local representative (**WSL2**) | None (ONNX not built on Linux → ONNX replay row recorded as skipped) | WSL2 (not native/cloud Linux); virtualized PMU (no LLC events, multiplexed); uncontrolled turbo; superseded by Durham for the paths Durham re-ran; `Full`-validation replay does not scale to 1M (uses `Light`) |
| [linux_performance_evaluation_2026_05_31.md](linux_performance_evaluation_2026_05_31.md) | **Historical / local development (Windows/MSYS2 laptop)** larger-corpus standard-vs-pooled evaluation + ready-to-run Linux `perf` helper | Local representative; perf counters were collected later (see WSL2 and Durham reports) | None | Windows/MSYS2 run; counter values not fabricated when `perf` was unavailable; superseded by Durham for the paths Durham re-ran |
| [perf_profile.md](perf_profile.md) | Linux `perf` profiling: WSL2 counters plus Durham HPC compute-node counters/hotspots, cache/branch reading, std-vs-pooled and SPSC interpretation, methodology | Local representative (**WSL2 + Durham HPC**) | None | Environment-specific PMU limits; `-O3` partial call graphs; raw `perf.data` not committed |

## Subdirectories

- `latency_histograms/` — supporting latency-histogram artefacts referenced by the inference/latency reports.
