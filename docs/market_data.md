# Recorded Market-Data Ingestion (Binance Public Depth)

This document describes Asterion's recorded public market-data path and how
Binance public order-book depth is normalised into Asterion's event schema.

> **This is a recorded public market-data engineering demo. It is not live trading, not authenticated exchange connectivity, and not evidence of equities-market realism.** No API keys, no order placement, no broker connectivity, no profitability claim. See [LIMITATIONS.md](../LIMITATIONS.md).

## Components

| Stage | Tool | Network? | Notes |
| --- | --- | --- | --- |
| Capture | [`tools/capture_binance_depth.py`](../tools/capture_binance_depth.py) | yes (opt-in, manual) | Public REST `GET /api/v3/depth` only; no keys. Never runs in CI. |
| Fixture | [`data/samples/binance_depth_sample.raw.jsonl`](../data/samples/binance_depth_sample.raw.jsonl) | no | Tiny hand-curated sample for deterministic CI. |
| Normalise | [`tools/normalise_binance_depth_to_asterion.py`](../tools/normalise_binance_depth_to_asterion.py) | no | Pure-Python core; writes CSV/binary via the `asterion` event-log writer. |
| Replay | `build/asterion_replay`, `scripts/asterion_inspect.py` | no | Existing deterministic replay/diagnostics pipeline. |

## Reviewer one-liners

```bash
# (1) Normalise the checked-in fixture into Asterion CSV + binary event logs.
python tools/normalise_binance_depth_to_asterion.py \
  --input data/samples/binance_depth_sample.raw.jsonl \
  --csv-output build/binance_sample.csv \
  --binary-output build/binance_sample.bin --json

# (2) Replay the normalised binary through the existing C++ replay engine.
./build/asterion_replay --input build/binance_sample.bin --format binary

# (3) Replay summary / checksums via the existing inspection CLI
#     (requires the built Python bindings on PYTHONPATH).
PYTHONPATH=build/python python scripts/asterion_inspect.py \
  replay-checksums --input build/binance_sample.bin --format binary --json
```

The checked-in `data/samples/binance_depth_sample.normalised.{csv,bin}` are the
committed golden outputs of step (1) on the fixture; tests assert they are
reproduced deterministically.

## Capturing your own public sample (manual, optional)

```bash
python tools/capture_binance_depth.py --symbol BTCUSDT --duration 20 \
  --max-events 40 --interval 1.0 \
  --output data/captures/btcusdt_depth.raw.jsonl
```

`data/captures/` is git-ignored: do not commit large raw captures. Capture uses
only the public REST depth endpoint, requires no API key, performs conservative
pacing with capped exponential backoff, and handles connection errors,
timeouts, malformed messages and Ctrl-C cleanly.

## Raw message shapes

The normaliser auto-detects two public shapes per JSONL line:

1. **Diff depth update** (`@depth` websocket diff stream shape):

   ```json
   {"e":"depthUpdate","E":1716950001000,"s":"BTCUSDT","U":1001,"u":1001,
    "b":[["60000.50","0.75000000"]],"a":[]}
   ```

2. **Book snapshot** (REST `/api/v3/depth` or partial-depth stream shape):

   ```json
   {"lastUpdateId":1000,"bids":[["60000.00","1.00000000"]],
    "asks":[["60001.00","1.50000000"]]}
   ```

The capture tool wraps each REST response in an envelope so the symbol and
capture time (absent from the REST depth body) survive:

```json
{"_captured_at_ns":1716950000000000000,"symbol":"BTCUSDT",
 "endpoint":"https://api.binance.com/api/v3/depth?symbol=BTCUSDT&limit=20",
 "data":{"lastUpdateId":1000,"bids":[...],"asks":[...]}}
```

Lines whose JSON object has a `_meta` or `_fixture` key are metadata banners and
are recorded but not normalised.

## Field mapping (Binance → Asterion)

Asterion's canonical event fields are:

```text
timestamp_ns,sequence_number,symbol_id,event_type,side,price_ticks,quantity,order_id,trade_id,flags
```

| Asterion field | Source |
| --- | --- |
| `timestamp_ns` | diff `E` (ms) × 1e6, or envelope `_captured_at_ns`, else previous + 1 |
| `sequence_number` | synthetic monotonic counter `1..N` over emitted events |
| `symbol_id` | deterministic small id per symbol (first-seen order; BTCUSDT → 1) |
| `event_type` | `Add` / `Replace` / `Cancel` / `Snapshot` (see rules below) |
| `side` | `Buy` for bids, `Sell` for asks |
| `price_ticks` | `round(Decimal(price) × 1e8)` — integer ticks, no float in the schema |
| `quantity` | `round(Decimal(qty) × 1e8)` — integer base units |
| `order_id` | **deterministic synthetic** id per resting price level (see below) |
| `trade_id` | `0` (depth data carries no trades) |
| `flags` | snapshot begin/end markers only |

### Diff-update rules (per price level)

* qty > 0, level not currently resting → **Add** (allocate a fresh synthetic id)
* qty > 0, level currently resting → **Replace** (reuse the level's synthetic id)
* qty == 0, level currently resting → **Cancel** (free the level's synthetic id)
* qty == 0, level not resting → no event; `level_remove_absent` diagnostic (info)

### Snapshot rule

A snapshot becomes an Asterion **snapshot block**: a begin marker
(`flags = kSnapshotBeginFlag`), one `Snapshot` record per resting level (each
with a fresh synthetic `order_id`), and an end marker (`flags = kSnapshotEndFlag`).
The replay engine resets and reconstructs the book from the block.

## The honest L2 → L3 limitation

Binance publishes **L2-style price-level** data: each update is a `[price, qty]`
level with **no per-order identity**. Asterion's book is **L3/order-oriented**:
every resting order has an `order_id`, a FIFO queue position and per-order state.

This adapter does **not** fabricate real exchange order IDs. Instead it uses
**level-replacement semantics with deterministic synthetic order IDs**:

* Each resting price level is modelled as exactly **one** synthetic order.
* The synthetic `order_id` is a monotonic counter value assigned the first time a
  level becomes resting, reused for `Replace` while it stays resting, and freed
  on `Cancel`. A level that disappears and reappears gets a **new** id (so the
  L3 book never sees a duplicate/reused order id, keeping replay clean).
* Per-level FIFO queue depth, individual order sizes, order arrival order within
  a level, and true order lifetimes are **not recoverable** from L2 data and are
  therefore not represented.

Consequently this path demonstrates **deterministic ingestion, normalisation and
book reconstruction**, not true L3 microstructure. Treat the synthetic order IDs
as a faithful *level* model, never as real order-level data.

## Normalisation diagnostics

The normaliser emits structured, deterministic diagnostics (severity + reason +
0-based source line index). Reasons:

| Reason | Severity | Trigger |
| --- | --- | --- |
| `malformed_message` | error | line is not valid JSON / not an object |
| `missing_fields` | error | no symbol available for a depth message |
| `unsupported_message_type` | warning | not a diff or snapshot (e.g. `trade`) |
| `non_monotonic_event_time` | warning | event time goes backwards for a symbol |
| `update_gap` | warning | diff `U` > previous `u` + 1 (missed updates) |
| `stale_update` | warning | diff `u` ≤ previous `u`, or snapshot id regressed |
| `crossed_book` | warning | best bid ≥ best ask after applying a message |
| `invalid_price` | error | price ≤ 0 or unparseable |
| `invalid_quantity` | error | quantity < 0 or unparseable |
| `level_remove_absent` | info | qty-0 update for a level not currently resting |

The diagnostics checksum uses a fixed FNV-1a recipe over `(line_index,
severity_code, reason_code)` — never Python's per-process-randomised `hash()` —
so it is stable across runs and machines.

## Determinism

* All emitted fields are integers derived deterministically from the input.
* Levels within a message are processed in a canonical order (bids best-first
  descending, asks best-first ascending), independent of source array order.
* Synthetic order ids are assigned by a deterministic monotonic counter.
* CSV/binary logs are written by the project's existing deterministic
  `asterion` event-log writer. Binary output is byte-identical across platforms;
  CSV equivalence is checked by reloading events (line endings aside).
