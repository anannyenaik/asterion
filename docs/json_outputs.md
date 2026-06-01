# Stable JSON Outputs

Asterion emits machine-readable JSON from its tools so reviewers and scripts can consume
results without parsing text. This note documents each output, the test that guards it,
and — importantly — which fields are **deterministic** (safe to diff/assert across runs)
versus **machine-dependent** (timings; never assert on these).

This is a lightweight contract note, not a formal schema framework. The authoritative
shape is the guarding test plus the checked-in sample fixture.

| Output | Produced by | Guarding test | Deterministic fields | Machine-dependent fields |
| --- | --- | --- | --- | --- |
| Benchmark JSON | `asterion_benchmarks --json` | `python/tests/test_cli.py::test_benchmark_summary_json`, `test_regression.py` (+ `data/samples/sample_benchmark_schema.json`) | `schema_version`, benchmark `name`, `category`, `samples`, allocation counts | `avg_ns`, p50/p95/p99/p99.9, `max_ns` |
| Latency-budget JSON | `asterion_latency_budget --json` | `python/tests/test_cli.py::test_latency_budget_summary_json` | `config_checksum`, stage names, `exceeded_count` (with default 0 budgets), budget values | observed/worst-case nanoseconds |
| SPSC replay JSON | `scripts/run_spsc_replay_demo.py --json` | `python/tests/test_spsc_replay.py` | `final_book_checksum`, `execution_report_checksum`, `diagnostics_checksum`, `checksum_parity`, `produced/consumed_events`, `dropped_events`, `max_queue_depth ≤ queue_capacity`, `end_of_stream_markers_consumed` | `elapsed_ns`, `throughput_events_per_second`, `backpressure_count` |
| Risk audit summary JSON | `asterion_inspect.py audit-summary --json` / `audit-verify --json` | `python/tests/test_cli.py::test_audit_summary_json`, `test_audit_verify_json` | `entry_count`, `accepted_count`, `rejected_count`, `check_counts`, `final_checksum`, `valid` | none |
| Inference benchmark JSON | `asterion_benchmarks --json` (`category="inference"` rows) | `python/tests/test_cli.py::test_benchmark_summary_json` | `backend`, `model name`, input shape, allocation count | per-call p50/p95/p99/p99.9/max ns (sub-µs dominated by timer resolution) |
| Audit manifest JSON | `asterion_inspect.py audit-manifest[-verify] --json` | `python/tests/test_cli.py::test_audit_manifest_cli_json` | `chain_checksum`, `file_count`, `ok`, `signature_present` | none |
| Replay parity JSON | `asterion_inspect.py replay-parity --json` | `python/tests/test_cli.py::test_replay_parity_cli_json` | per-symbol/combined checksum agreement flags | none |
| ONNX status JSON | `asterion_inspect.py onnx-status --json` | `python/tests/test_cli.py::test_onnx_status_cli_json` | `onnx` availability flags, backend names | none |
| Portfolio-risk JSON | `asterion_inspect.py portfolio-risk --json` | `python/tests/test_cli.py::test_portfolio_risk_cli_json` | exposures, `snapshot_checksum`, breach state | none |

## Rules for consumers

- **Never assert on nanosecond/throughput fields** in tests or regression gates — they are
  machine-dependent. Assert on checksums, counts, parity flags and config checksums instead.
- Malformed/missing-input cases still return parseable JSON with an `error` field; this is
  covered by `test_cli.py::test_json_error_for_malformed_json` and friends.
- Benchmark/latency/regression JSON are only comparable across runs produced on the **same**
  controlled hardware (see [BENCHMARKS.md](../BENCHMARKS.md) and [LIMITATIONS.md](../LIMITATIONS.md)).
