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
- `risk`: pre-trade limits, duplicate client-order-ID tracking, stale-data policy, kill switch,
  working-exposure lifecycle handling, simulated portfolio-risk accounting and deterministic audit
  trails/logs/manifests.
- `session`: simulated broker/session lifecycle state machine for deterministic order-lifecycle
  tests. It is not a live broker adapter.
- `strategy`: small deterministic strategies used as workloads, not profitability claims.
- `inference`: model interface, deterministic linear backend, feature extraction,
  measured latency accounting and timeout/late-signal policy hooks.
- `telemetry`: latency histogram, lightweight metrics and per-stage latency-budget accounting.
- `python/asterion`: thin bindings and analysis helpers for replay, checksums, aggregate views,
  risk exposure snapshots, benchmark JSON summaries and offline benchmark/latency-budget regression
  analysis.
- `scripts/asterion_inspect.py`: a single inspection CLI over replay checksums, diagnostics,
  per-symbol summaries, replay parity, risk exposure fixtures, simulated portfolio snapshots,
  audit logs/manifests, optional ONNX status, rate-limit mode, latency-budget JSON, benchmark JSON
  and benchmark regression comparison.

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
The v1 schema boundary is captured in `data/schema/event_log_schema_v1.json` and explained in
`docs/event_log_schema.md`; breaking changes to CSV columns, binary layout, enum wire values,
snapshot flag semantics or fixture formats require an explicit migration note and fixture
regeneration.

### Recorded Binance Public Depth Normalisation

`tools/normalise_binance_depth_to_asterion.py` is an honest adapter from recorded
**public** Binance order-book depth into the same canonical event schema used by
every other ingestion path. Binance publishes L2-style price-level data with no
per-order identity, while Asterion's book is L3/order-oriented. The adapter uses
**level-replacement semantics with deterministic synthetic order IDs**: each
resting price level is one synthetic order whose id is a monotonic counter value
assigned on first appearance, reused for `Replace` while resting and freed on
`Cancel`. It deliberately does **not** fabricate real exchange order IDs, and the
limitation is documented in `docs/market_data.md` and `LIMITATIONS.md`. The
parsing/diagnostics core is pure-Python (no network, no compiled extension); only
writing CSV/binary logs reuses the existing `asterion` event-log writer. Capture
(`tools/capture_binance_depth.py`) uses only the public REST depth endpoint, has
no keys and is opt-in/manual — it never runs in CI. A tiny hand-curated fixture
drives deterministic tests. This is a recorded market-data engineering demo, not
live trading, authenticated connectivity or an equities-realism claim.

### Replay Diagnostics

Replay emits structured diagnostics with event index, sequence number, symbol, severity and reason.
Errors are included in a diagnostics checksum so malformed streams are reproducible artifacts too.
The replay engine still owns one symbol book per run. Aggregate multi-symbol views default to
grouping events by symbol and running that same engine per group, reporting per-symbol counts,
first/last sequence, diagnostics and checksum summaries. `MultiSymbolBookSet` now also backs an
opt-in shared replay path that routes an interleaved stream in a single pass, preserves the same
summary shape and exposes a deterministic combined book checksum. Tests compare the shared path to
the grouped path on deterministic generated streams, fixed-seed fuzz cases and malformed
multi-symbol streams. It is a routing/replay surface, not a cross-symbol matching engine, and
grouped replay remains the default.
Structured parity reports compare every per-symbol checksum plus combined-book and aggregate
checksums. They document coverage for the opt-in shared path; they are not a reason to make shared
replay the default without exhaustive validation.

### L3 Internally, L2 Externally

The book stores L3 state internally because matching and replay correctness depend on individual order IDs and FIFO priority. L2 views are generated from that state for strategy workloads, feature extraction and external inspection.
The value-returning `l2_view(...)` API remains for convenience, while `fill_l2_view(...)` lets
callers reuse preallocated bid/ask vectors in measured paths.

### Measured Hot Path

The benchmark target includes a scoped end-to-end path over `sample_hot_path_replay.bin`: binary
events are replayed into the L3 book, a reusable L2 view is filled, `ImbalanceStrategy` emits a
fixed-size decision batch and the risk gateway checks the resulting orders. The path deliberately
keeps logging and string formatting outside the measured event loop. It is representative
instrumentation for this codebase, not a production HFT architecture claim.

### Risk Gateway

The risk gateway exists before matching because serious trading systems reject invalid or dangerous
orders before they consume matching resources. The current checks are intentionally simple but real:
quantity, notional, position, gross exposure, price band, duplicate ID, stale data and kill switch.
Accepted client-order IDs are tracked with a small flat set so callers can reserve capacity and avoid
per-order node allocations in warmed risk-only paths.
Additional controls are opt-in and disabled by default so the existing hot path is unchanged:
open-order (working) exposure per symbol, per-client fixed or sliding-window message-rate limiting
and self-trade prevention against a client's own resting orders. Accepted resting orders can be
resized or released from execution reports, with manual `release_order` kept as a fallback.
Replace-order risk uses the tracked resting order and exchange-order mapping to re-check quantity,
notional, price band, working exposure delta, position exposure, duplicate command IDs and
self-trade risk before mutating tracked exposure. Enabling the kill switch cancels tracked simulated
working exposure and blocks new orders. Simulated disconnect state is also explicit:
cancel-on-disconnect is opt-in, and the disconnected new-order policy is configured rather than
implied. Only the reject paths that matter for the hot path stay allocation-free; working-order and
self-trade state is built only when those controls are enabled.

`SimulatedBrokerSession` is a deterministic lifecycle model around this risk state: it records
connect/disconnect/reconnect, order accepted, pending cancel, cancel ack/reject and fill events with
a checksum. It can call into `RiskGateway` on disconnect/reconnect, but it never owns a socket or
sends live broker messages.

`PortfolioRiskMonitor` is deliberately separate from `RiskGateway`. It is an opt-in simulated
accounting gate over caller-supplied marks, signed positions and fills, with deterministic checks
for gross exposure, net exposure, concentration and mark-to-market loss. Audit recording is opt-in,
matching the risk gateway's default allocation posture.

### Inference In The Measured Path

The inference module is deliberately small. `LinearModel` is deterministic and can run inside an
event loop, which makes it suitable for latency measurement. `MeasuredInferenceEngine` records
model-score latency separately from replay and matching, then applies timeout and late-signal policy
hooks. A TorchScript-style class documents the external-model boundary, but it is a placeholder until
LibTorch or another runtime is deliberately linked. This infrastructure measures plumbing cost; it
does not imply a profitable model.

The timeout/late-signal policy has two layers. `evaluate_inference_policy` is a pure function of
injected `(observed_latency_ns, signal_timestamp_ns, now_timestamp_ns)` values: a signal is accepted
within budget, abstained when the model is over its timeout, and abstained when the signal is older
than `max_signal_age_ns`. `InferencePolicyGate` wraps that pure function with the only state the
policy needs — a consecutive late-signal counter and a latched `disabled` flag. When
`disable_on_repeated_late_signals` is configured, the gate disables the model after
`max_consecutive_late_signals` consecutive late signals and abstains on every subsequent call until
`reset()`. Because the gate is driven by injected timings rather than the wall clock, its disable
behaviour is unit-tested deterministically.

The benchmark runner reports inference timings under a separate `inference` category so model and
feature-extraction cost is never folded into the trading hot path. It covers feature extraction only,
LinearModel inference only, feature extraction + LinearModel, the measured-engine path, the
event-loop policy-gate overhead, and — only when built with ONNX Runtime — ONNX inference only and
feature extraction + ONNX. Each inference benchmark records its backend, model name and input shape
alongside the latency distribution and allocation count.

Backend selection is explicit: `make_inference_backend` returns a `Model` for the requested backend
and records whether it fell back. The deterministic `LinearModel` is the default and the fallback. An
optional ONNX Runtime backend (`OnnxModel`) is compiled only behind the `ASTERION_USE_ONNXRUNTIME`
CMake flag when the dependency is found; otherwise an ONNX request degrades to `LinearModel` with an
honest detail string. The dependency is never required by default CI; a manual CI input configures
with `-DASTERION_USE_ONNXRUNTIME=ON` to exercise the build flag and deterministic fallback path.
The compile-time `kOnnxRuntimeAvailable` constant lets tests branch on the build configuration. A
tiny identity ONNX fixture is checked in as base64 and decoded only in real ONNX Runtime test builds.

### Latency Budget Accounting

`LatencyBudgetAccountant` separates two concerns that are easy to conflate. The accounting logic
(worst-case and total per stage, utilization, budget breaches and worst-offender selection) is a
pure function of the durations it is fed, so it is unit-tested deterministically with injected
values. The `asterion_latency_budget` tool wires real `monotonic_now_ns()` measurements into that
same logic for ad-hoc inspection. Budgets are configurable and default to unset; the tool emits a
deterministic config checksum but never claims the measured nanoseconds are portable.

### Risk Audit Trail

Each pre-trade decision is appended to a `RiskAuditTrail` with the deciding check name, decision,
reject reason and the relevant limit and observed values. Optional append-only text/JSONL logging
writes the same entries plus the cumulative deterministic checksum. Rotation by record count or byte
size is opt-in and uses deterministic file naming. Verification tooling recomputes the checksum
across one or more audit files, including rotated files. Entries depend only on the order flow and
configured limits, not on wall-clock time, so the trail can be compared across runs and machines -
the same auditability property the book and execution-report checksums already provide.

Audit manifests add a file-level layer over those logs: record count, byte size, raw content
checksum, cumulative audit-chain checksum and an optional HMAC-SHA256 signature. Signing is disabled
by default and depends on caller-managed local key material; it is tamper-evident tooling, not
managed retention or compliance storage.

### Offline Regression Tooling

Benchmark regression comparison and JSON inspection live in a pure-stdlib Python module
(`asterion.regression`) that never imports the compiled extension. This keeps the comparison logic
independently testable and usable without a built project, and keeps machine-dependent performance
comparison clearly separated from the deterministic correctness path.
