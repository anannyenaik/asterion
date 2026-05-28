# Design

Asterion is organized around a deterministic tick-to-trade path. Each module has a narrow responsibility so correctness tests can isolate failures and benchmarks can measure meaningful boundaries.

## Tick-To-Trade Pipeline

```text
recorded/simulated market data event
  -> log decoding and sequence validation
  -> L3 order book mutation
  -> diagnostics, invariant and checksum path
  -> L2 projection
  -> strategy / feature extraction / model score
  -> risk gateway
  -> matching engine
  -> execution report
  -> telemetry and checksums
```

The first implementation keeps the hot path simple and auditable. It uses integer prices, deterministic containers and explicit report structures. There are no custom allocators, networking layers or concurrency claims in this version.

## Module Responsibilities

- `core`: shared integer types, monotonic clock helpers and deterministic checksum utilities.
- `market_data`: fixed event schema, CSV/binary log IO, deterministic replay diagnostics and
  synthetic event generation.
- `book`: L3 order book, price levels, FIFO queues, L2 views and invariant checks.
- `matching`: price-time-priority matching and execution reports.
- `risk`: pre-trade limits, duplicate client-order-ID tracking, stale-data policy, kill switch
  and a deterministic pre-trade audit trail.
- `strategy`: small deterministic strategies used as workloads, not profitability claims.
- `inference`: model interface, deterministic linear backend, feature extraction,
  measured latency accounting and timeout/late-signal policy hooks.
- `telemetry`: latency histogram, lightweight metrics and per-stage latency-budget accounting.
- `python/asterion`: thin bindings and analysis helpers for replay, checksums, aggregate views,
  benchmark JSON summaries and offline benchmark/latency-budget regression analysis.
- `scripts/asterion_inspect.py`: a single inspection CLI over replay checksums, diagnostics,
  per-symbol summaries, latency-budget JSON, benchmark JSON and benchmark regression comparison.

## Data Flow

Market-data replay mutates an L3 book directly from Add, Cancel, Replace and Execute events.
Snapshot events reset and reload book state (framed with begin/end flags in the event `flags`
field), while Trade and Heartbeat events are represented in the same schema for recorded-log
compatibility. Order-entry flow passes through the risk gateway before reaching the matching
engine. The matching engine owns order states, emits execution reports and mutates its book when
orders rest or trade.

## Design Decisions

### Deterministic Replay

Replay is deterministic so a final checksum can be compared across runs, machines and future code changes. This is essential for debugging book reconstruction and matching behavior because it turns a stream of market events into a reproducible artifact.

### Recorded Event Logs

CSV and binary logs share the same canonical event fields and produce the same event-stream
checksum when they contain equivalent events. The binary format is intentionally small and
explicit: a fixed `ASTITCH1` header followed by 58-byte little-endian records. The parser validates
magic bytes, schema version, record size, event type, side and truncated records before replay.

### Replay Diagnostics

Replay emits structured diagnostics with event index, sequence number, symbol, severity and reason.
Errors are included in a diagnostics checksum so malformed streams are reproducible artifacts too.
The replay engine still owns one symbol book per run. Aggregate multi-symbol views group events by
symbol and run that same engine per group, reporting per-symbol counts, first/last sequence,
diagnostics and checksum summaries. This is intentionally not described as full multi-symbol
matching. `MultiSymbolBookSet` adds optional groundwork: it owns one `OrderBook` per symbol and
routes an interleaved stream in a single pass, exposing a deterministic combined checksum. It is a
routing surface, not a cross-symbol matching engine, and it does not replace the grouped replay path,
which remains the validated default.

### L3 Internally, L2 Externally

The book stores L3 state internally because matching and replay correctness depend on individual order IDs and FIFO priority. L2 views are generated from that state for strategy workloads, feature extraction and external inspection.

### Risk Gateway

The risk gateway exists before matching because serious trading systems reject invalid or dangerous orders before they consume matching resources. The current checks are intentionally simple but real: quantity, notional, position, gross exposure, price band, duplicate ID, stale data and kill switch. Three further controls are opt-in and disabled by default so the existing hot path is unchanged: open-order (working) exposure per symbol, per-client fixed-window message-rate limiting and self-trade prevention against a client's own resting orders. Only the reject paths that matter for the hot path stay allocation-free; working-order and self-trade state is built only when those controls are enabled.

### Inference In The Measured Path

The inference module is deliberately small. `LinearModel` is deterministic and can run inside an
event loop, which makes it suitable for latency measurement. `MeasuredInferenceEngine` records
model-score latency separately from replay and matching, then applies timeout and late-signal policy
hooks. A TorchScript-style class documents the external-model boundary, but it is a placeholder until
LibTorch or another runtime is deliberately linked. This infrastructure measures plumbing cost; it
does not imply a profitable model.

Backend selection is explicit: `make_inference_backend` returns a `Model` for the requested backend
and records whether it fell back. The deterministic `LinearModel` is the default and the fallback. An
optional ONNX Runtime backend (`OnnxModel`) is compiled only behind the `ASTERION_USE_ONNXRUNTIME`
CMake flag when the dependency is found; otherwise an ONNX request degrades to `LinearModel` with an
honest detail string. The dependency is never required by CI, and the compile-time
`kOnnxRuntimeAvailable` constant lets tests branch on the build configuration.

### Latency Budget Accounting

`LatencyBudgetAccountant` separates two concerns that are easy to conflate. The accounting logic
(worst-case and total per stage, utilization, budget breaches and worst-offender selection) is a
pure function of the durations it is fed, so it is unit-tested deterministically with injected
values. The `asterion_latency_budget` tool wires real `monotonic_now_ns()` measurements into that
same logic for ad-hoc inspection. Budgets are configurable and default to unset; the tool emits a
deterministic config checksum but never claims the measured nanoseconds are portable.

### Risk Audit Trail

Each pre-trade decision is appended to a `RiskAuditTrail` with the deciding check name, decision,
reject reason and the relevant limit and observed values. Entries depend only on the order flow and
configured limits, not on timing, so the trail produces a deterministic checksum that can be
compared across runs and machines — the same auditability property the book and execution-report
checksums already provide.

### Offline Regression Tooling

Benchmark regression comparison and JSON inspection live in a pure-stdlib Python module
(`asterion.regression`) that never imports the compiled extension. This keeps the comparison logic
independently testable and usable without a built project, and keeps machine-dependent performance
comparison clearly separated from the deterministic correctness path.
