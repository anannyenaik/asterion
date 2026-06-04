# Recorded Public Crypto L2 Market-Data Normalisation And Deterministic Replay Evidence

> This is recorded public crypto L2 market-data normalisation and deterministic replay evidence. It is not live trading, not authenticated exchange connectivity, not order placement, not broker connectivity, not equities-market realism, not L3 exchange-feed realism, not alpha/profitability evidence and not production market-data infrastructure.

## Executive Summary

A larger checked-in Binance public depth fixture was captured from the unauthenticated public REST
`GET /api/v3/depth` endpoint, normalised into Asterion CSV and binary event logs, replayed through
the deterministic replay engine, and checked for diagnostics, CSV/binary equivalence and
grouped/shared replay parity.

The fixture is intentionally compact for Git review and CI: 9 public REST depth snapshots at 10 bid
levels and 10 ask levels per snapshot. Each snapshot becomes one Asterion snapshot block, so the
normalised log contains 198 snapshot events. All checksums below are generated from the committed
fixture and current tooling; no benchmark or latency numbers are claimed.

## Data Source And Fixture Method

Capture command used:

```bash
python tools/capture_binance_depth.py --symbol BTCUSDT --duration 8 \
  --max-events 12 --interval 0.5 --limit 10 --timeout 5 \
  --output data/samples/binance_depth_larger_sample.raw.jsonl
```

Capture result:

- Source: Binance public REST depth endpoint, `https://api.binance.com/api/v3/depth`.
- Endpoint path: `/api/v3/depth`.
- Symbol: `BTCUSDT`.
- Depth limit: 10 bid levels and 10 ask levels.
- Capture window: `2026-06-04T04:12:31Z` to `2026-06-04T04:12:39Z`.
- Captured messages: 9.
- Capture errors: 0.
- Raw fixture: `data/samples/binance_depth_larger_sample.raw.jsonl`.
- Capture sidecar: `data/samples/binance_depth_larger_sample.raw.jsonl.meta.json`.

The capture tool uses no API keys, no account endpoint, no signed endpoint and no order-placement
endpoint. The checked-in fixture is replayed offline; CI does not capture from the network.

## What This Case Study Proves

- Public recorded Binance L2 depth snapshots can be converted deterministically into Asterion's
  event-log schema.
- The committed CSV and binary event logs are semantically equivalent.
- The deterministic replay engine produces stable event-log, final-book, execution-report and
  diagnostics checksums on the larger fixture.
- Grouped replay and the opt-in shared replay path agree on combined-book and aggregate checksums.
- The fixture guard can regenerate the normalised outputs and catch drift in counts, checksums,
  binary layout, replay diagnostics and grouped/shared parity.

## What This Case Study Does Not Prove

- It does not prove live trading, live market-data connectivity, broker connectivity or order
  placement.
- It does not prove authenticated exchange access.
- It does not prove equities-market realism or L3 exchange-feed realism.
- It does not prove alpha, profitability, predictive quality or signal value.
- It does not prove production market-data infrastructure or production HFT performance.
- It does not include portable latency, throughput or benchmark evidence.

## Normalisation Pipeline

Pipeline:

```text
public recorded depth data
-> deterministic normaliser
-> Asterion CSV event log
-> binary event log
-> deterministic replay
-> diagnostics and checksums
-> grouped/shared parity check
```

Normalisation command:

```bash
PYTHONPATH=build/python python tools/normalise_binance_depth_to_asterion.py \
  --input data/samples/binance_depth_larger_sample.raw.jsonl \
  --csv-output data/samples/binance_depth_larger_sample.normalised.csv \
  --binary-output data/samples/binance_depth_larger_sample.normalised.bin \
  --json
```

Binance public depth data is L2 price-level data. Asterion's event schema is order-oriented, so the
normaliser uses documented level-replacement semantics and deterministic synthetic order IDs. In
this larger REST snapshot fixture, every raw message is a snapshot envelope. Each 20-level snapshot
is emitted as:

- one snapshot-begin marker;
- 20 snapshot level records;
- one snapshot-end marker.

That gives `9 * 22 = 198` normalised snapshot events.

## Replay Pipeline

Replay and inspection commands:

```bash
PYTHONPATH=build/python python scripts/asterion_inspect.py \
  replay-checksums --input data/samples/binance_depth_larger_sample.normalised.bin \
  --format binary --json

PYTHONPATH=build/python python scripts/asterion_inspect.py \
  diagnostics --input data/samples/binance_depth_larger_sample.normalised.bin \
  --format binary --json

PYTHONPATH=build/python python scripts/asterion_inspect.py \
  replay-parity --input data/samples/binance_depth_larger_sample.normalised.csv \
  --format csv --json
```

The same replay fields match for CSV and binary inputs.

## Evidence Table

| Metric | Value | Source command/tool | Caveat |
| --- | ---: | --- | --- |
| JSONL line count | 10 | `data/samples/binance_depth_larger_sample.expected.json` | Includes one metadata banner |
| Raw message count | 9 | `normalise_binance_depth_to_asterion.py --json` | Public REST snapshots only |
| Metadata/header count | 1 | `normalise_binance_depth_to_asterion.py --json` | Not normalised into events |
| Depth levels per message | 20 | Capture sidecar + expected manifest | 10 bid + 10 ask levels |
| First/last `lastUpdateId` | 94843248537 / 94843253413 | Expected manifest parsed from raw JSONL | REST snapshot sequence IDs, not L3 order IDs |
| Timestamp range ns | 1780546352192403700 / 1780546359343295100 | Normaliser report | Capture timestamps from REST envelopes |
| Symbol mapping | `BTCUSDT -> 1` | Normaliser report | Deterministic first-seen mapping |
| Normalised event count | 198 | Normaliser report | Snapshot events only |
| Event type counts | `snapshot: 198` | Normaliser report | No diff updates in this larger REST fixture |
| Normalisation diagnostics | 0 | Normaliser report | Clean fixture |
| Normaliser events checksum | 16969322721924544686 | Normaliser report | Deterministic FNV-style checksum |
| Normaliser diagnostics checksum | 14695981039346656037 | Normaliser report | Empty diagnostics checksum |
| CSV event checksum | 16969322721924544686 | `asterion.read_event_log(..., csv)` | Semantic event checksum |
| Binary event checksum | 16969322721924544686 | `asterion.read_event_log(..., binary)` | Semantic event checksum |
| CSV/binary tuple equivalence | `true` | `python/tests/test_binance_normalise.py` | Event tuples match, independent of encoding |
| Replay events processed | 198 | `scripts/asterion_inspect.py replay-checksums` | Binary and CSV agree |
| Replay final-book checksum | 8948256044359602305 | `scripts/asterion_inspect.py replay-checksums` | Correctness checksum, not performance |
| Replay diagnostics checksum | 14695981039346656037 | `scripts/asterion_inspect.py diagnostics` | Empty diagnostics checksum |
| Replay warnings/errors | 0 / 0 | `scripts/asterion_inspect.py diagnostics` | Clean fixture |
| Grouped aggregate checksum | 1312278323814029705 | `scripts/asterion_inspect.py per-symbol` | One symbol |
| Grouped combined book checksum | 4174707875873495323 | `scripts/asterion_inspect.py per-symbol` | One symbol |
| Grouped/shared parity | `matched`, 0 mismatches | `scripts/asterion_inspect.py replay-parity` | Shared replay remains opt-in |
| Timing/throughput | not measured | Not run | No benchmark claim in this report |

## Diagnostics Summary

The clean larger fixture produces zero normalisation diagnostics and zero replay diagnostics.

Failure-mode coverage remains deterministic and network-free. The Python tests exercise missing or
gapped update sequence, stale update, malformed record, invalid price and invalid quantity
diagnostics. The larger fixture regeneration guard also checks CSV/binary event tuple equivalence,
binary header/record layout, replay checksums and grouped/shared parity. No corrupted binary fixture
is committed.

## Checksum And Equivalence Summary

| Check | Result |
| --- | --- |
| Normaliser event checksum | 16969322721924544686 |
| CSV event checksum | 16969322721924544686 |
| Binary event checksum | 16969322721924544686 |
| CSV/binary event tuple equivalence | true |
| Replay event-log checksum | 16969322721924544686 |
| Replay final-book checksum | 8948256044359602305 |
| Replay execution-report checksum | 14695981039346656037 |
| Replay diagnostics checksum | 14695981039346656037 |
| Replay warning/error count | 0 / 0 |

## Grouped Versus Shared Replay Parity

| Field | Grouped | Shared | Match |
| --- | ---: | ---: | --- |
| Symbol count | 1 | 1 | true |
| Combined book checksum | 4174707875873495323 | 4174707875873495323 | true |
| Aggregate checksum | 1312278323814029705 | 1312278323814029705 | true |
| Mismatch count | 0 | 0 | true |

## Limitations

- Binance public depth is L2 price-level data, not L3 order-level data. Synthetic order IDs are a
  deterministic engineering adapter, not real exchange order IDs.
- The larger fixture uses public REST snapshots, not a live websocket feed and not authenticated
  exchange connectivity.
- REST depth exposes only the requested top-of-book window; levels outside the depth limit are not
  observed.
- This is a compact checked-in fixture for review and CI, not a large research corpus.
- No timing or throughput was measured for this case study.
- Native Linux `perf` evidence remains deferred on the current Windows laptop because firmware
  virtualisation is disabled, preventing usable WSL2 Linux/PMU access.

## Next Work

- Optionally add a replay-loop-with-ONNX benchmark row when the optional ONNX Runtime lane is
  available and the claim boundary is kept to systems cost only.
- Collect native Linux `perf` evidence on hardware where virtualization/PMU access is available.
- Write the technical paper only after native Linux perf evidence is collected.
