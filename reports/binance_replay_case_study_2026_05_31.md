# Recorded Binance Public Depth-Stream Engineering Case Study (2026-05-31)

> Recorded public crypto L2 normalisation and deterministic replay case study.

The path uses no API keys and performs no order placement. It evaluates recorded
market-data engineering rather than live connectivity, L3 feed fidelity,
equities-market realism or profitability.
This case study captures (or replays a checked-in fixture of) **public** Binance
order-book depth, normalises it into Asterion's existing event schema, and
replays it deterministically through the existing replay/diagnostics pipeline.

## What was added

| Item | Path |
| --- | --- |
| Public depth capture tool (REST, no keys, opt-in/manual) | `tools/capture_binance_depth.py` |
| L2→L3 normaliser (Binance depth → Asterion events) | `tools/normalise_binance_depth_to_asterion.py` |
| Checked-in raw fixture (hand-curated, tiny) | `data/samples/binance_depth_sample.raw.jsonl` |
| Fixture metadata sidecar | `data/samples/binance_depth_sample.meta.json` |
| Committed normalised CSV (golden) | `data/samples/binance_depth_sample.normalised.csv` |
| Committed normalised binary (golden) | `data/samples/binance_depth_sample.normalised.bin` |
| Fixture regeneration/checksum guard | `data/samples/binance_depth_sample.expected.json` |
| Deterministic tests | `python/tests/test_binance_normalise.py` |
| Mapping / methodology doc | `docs/market_data.md` |

## Source

- **Source stream type:** public REST order-book depth (`GET /api/v3/depth`),
  recorded; the normaliser also accepts the public `@depth` diff-stream shape.
- **Symbol:** `BTCUSDT` (assigned Asterion `symbol_id = 1`).
- **Sample size:** the checked-in fixture is a tiny hand-curated sample
  exercising one REST snapshot envelope plus four depth-diff updates.
- **Capture status:** no live capture was performed for this report; the
  **checked-in fixture path was used**. To capture your own public sample
  locally, see the command below (manual, opt-in; never run in CI).

## Exact commands

Capture (optional, manual, requires network; public endpoint, no keys):

```bash
python tools/capture_binance_depth.py --symbol BTCUSDT --duration 20 \
  --max-events 40 --interval 1.0 \
  --output data/captures/btcusdt_depth.raw.jsonl
```

Normalise the checked-in fixture (deterministic, no network):

```bash
python tools/normalise_binance_depth_to_asterion.py \
  --input data/samples/binance_depth_sample.raw.jsonl \
  --csv-output data/samples/binance_depth_sample.normalised.csv \
  --binary-output data/samples/binance_depth_sample.normalised.bin \
  --json
```

Replay through the existing pipeline:

```bash
./build/asterion_replay --input data/samples/binance_depth_sample.normalised.bin --format binary
PYTHONPATH=build/python python scripts/asterion_inspect.py \
  replay-checksums --input data/samples/binance_depth_sample.normalised.bin --format binary --json
PYTHONPATH=build/python python scripts/asterion_inspect.py \
  per-symbol --input data/samples/binance_depth_sample.normalised.csv --json
PYTHONPATH=build/python python scripts/asterion_inspect.py \
  replay-parity --input data/samples/binance_depth_sample.normalised.csv --json
```

## Results (measured on the checked-in fixture)

These values are deterministic and reproduced by the test suite. They are
correctness checksums, not performance numbers.

### Normalisation

| Metric | Value |
| --- | --- |
| Raw messages (excl. metadata banner) | 5 |
| Metadata banner lines | 1 |
| Normalised Asterion events | 11 |
| Event-type breakdown | Snapshot 6, Add 2, Replace 1, Cancel 2 |
| Symbols | `BTCUSDT` → `symbol_id` 1 |
| First / last `timestamp_ns` | 1716950000000000000 / 1716950004000000000 |
| Normalisation diagnostics | 0 |
| Normaliser `events_checksum` | 9673906315134520083 |
| Normaliser `diagnostics_checksum` | 14695981039346656037 (empty-set FNV offset) |

The 11 events are: a snapshot block (begin marker + 4 levels + end marker = 6),
then two diff `Add`s, one `Replace` and two `Cancel`s.

The fixture guard re-runs normalisation from the raw JSONL, compares regenerated
CSV lines with the committed CSV, checks binary event-log properties by loading
events, validates replay checksums against the expected manifest, and asserts
CSV/binary semantic event tuple equivalence. It uses only the tiny checked-in
fixture and does not touch the network.

### Replay (identical for CSV and binary inputs)

| Metric | Value |
| --- | --- |
| `events_processed` | 11 |
| `sequence_valid` | true |
| `event_log_checksum` | 9673906315134520083 |
| **`final_book_checksum`** | **2539005926052284398** |
| `execution_report_checksum` | 14695981039346656037 (no executions in depth data) |
| Replay `diagnostics_checksum` | 14695981039346656037 |
| Replay diagnostic errors / warnings | 0 / 0 |

### Aggregate / parity

| Metric | Value |
| --- | --- |
| `aggregate_checksum` | 33842106777839346 |
| `combined_book_checksum` | 17968467496673997210 |
| Grouped-vs-shared parity | matched (0 mismatches) |

### Diagnostics summary

The clean fixture produces **0 normalisation diagnostics** and **0 replay
diagnostics**. The structured normalisation diagnostics (`malformed_message`,
`missing_fields`, `unsupported_message_type`, `non_monotonic_event_time`,
`update_gap`, `stale_update`, `crossed_book`, `invalid_price`,
`invalid_quantity`, `level_remove_absent`) are exercised by deterministic unit
tests that feed deliberately malformed/gappy/crossed inputs (no network).

### CSV/binary equivalence

CSV and binary normalised logs load to identical event lists and produce
identical replay checksums (`final_book_checksum`, `execution_report_checksum`,
`diagnostics_checksum`, `event_log_checksum` all equal across formats). The
binary writer is byte-deterministic across repeated runs.

### Replay throughput

**Not measured for this case study.** No throughput/latency numbers are claimed
here. General (non-portable, local) benchmark methodology lives in
[BENCHMARKS.md](../BENCHMARKS.md); this case study is about deterministic
ingestion/normalisation/replay correctness, not performance.

## Explicit limitations

- **L2 → L3 modelling.** Binance publishes L2 price-level data with no per-order
  identity. Asterion's book is L3/order-oriented. The normaliser is a deterministic
  adapter using **level-replacement semantics with deterministic synthetic order
  IDs**; it does **not** fabricate real exchange order IDs. Per-level FIFO depth,
  individual order sizes/arrival order and true order lifetimes are not
  recoverable from L2 data and are not represented. See
  [docs/market_data.md](../docs/market_data.md) and [LIMITATIONS.md](../LIMITATIONS.md).
- **Fixture, not a live feed.** The checked-in sample is tiny and hand-curated
  for deterministic CI. It is clearly marked as a fixture and is not a real
  capture. Local capture is opt-in/manual and never runs in CI.
- **Public market data only.** Capture uses the public REST depth endpoint with
  no API key. There is no authenticated access, no order entry and no trading.
- **No profitability or microstructure claim.** This demonstrates deterministic
  engineering (capture → normalise → replay → checksums), not a trading edge,
  signal quality, or production market-data infrastructure.
- **REST snapshot windowing.** REST depth returns a fixed top-of-book window;
  levels outside the captured depth are not observed. The fixture avoids this by
  construction; real captures inherit the endpoint's depth limit.

## Reproducing

```bash
# Build (Release, with Python bindings) then run the deterministic tests.
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_PYTHON=ON
cmake --build build
PYTHONPATH=build/python python -m pytest python/tests/test_binance_normalise.py -v
```

Default CI does not depend on Binance availability: every test above runs on the
checked-in fixture or in-memory inputs with no network access.
