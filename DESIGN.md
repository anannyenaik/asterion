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
- `risk`: pre-trade limits, duplicate client-order-ID tracking, stale-data policy and kill switch.
- `strategy`: small deterministic strategies used as workloads, not profitability claims.
- `inference`: model interface, linear model and feature extraction placeholder.
- `telemetry`: latency histogram and lightweight metrics.

## Data Flow

Market-data replay mutates an L3 book directly from Add, Cancel, Replace and Execute events.
Trade, Snapshot and Heartbeat events are represented in the same schema for recorded-log
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
The current replay engine owns one symbol book per run; multi-symbol generated logs can be replayed
per symbol, while aggregate multi-symbol replay remains a future extension.

### L3 Internally, L2 Externally

The book stores L3 state internally because matching and replay correctness depend on individual order IDs and FIFO priority. L2 views are generated from that state for strategy workloads, feature extraction and external inspection.

### Risk Gateway

The risk gateway exists before matching because serious trading systems reject invalid or dangerous orders before they consume matching resources. The current checks are intentionally simple but real: quantity, notional, position, gross exposure, price band, duplicate ID, stale data and kill switch.

### Inference In The Measured Path

The inference module is deliberately small. `LinearModel` is deterministic and can run inside an event loop, which makes it suitable for latency measurement. Future ONNX or TorchScript integration should preserve the same measured boundary instead of hiding model cost outside the pipeline.
