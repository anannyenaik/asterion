# Event-Log Schema v1

Asterion event logs have one canonical market-data event schema with two encodings:
CSV and a compact binary format. The current manifest is
[`data/schema/event_log_schema_v1.json`](../data/schema/event_log_schema_v1.json).

Schema v1 is stable for checked-in fixtures. Asterion does not yet provide a full
backward-compatible migration framework. Breaking schema changes are allowed, but they
must be intentional: bump the schema version, add/update the schema manifest, document
the migration, regenerate affected fixtures and keep the drift tests clear.

## Canonical Fields

CSV column order and binary record order are:

```text
timestamp_ns,sequence_number,symbol_id,event_type,side,price_ticks,quantity,order_id,trade_id,flags
```

Types:

| Field | CSV | Binary |
| --- | --- | --- |
| `timestamp_ns` | base-10 integer | int64 at offset 0 |
| `sequence_number` | base-10 integer | uint64 at offset 8 |
| `symbol_id` | base-10 integer | uint32 at offset 16 |
| `event_type` | canonical spelling | uint8 at offset 20 |
| `side` | canonical spelling | uint8 at offset 21 |
| `flags` | base-10 integer | uint32 at offset 22 |
| `price_ticks` | base-10 integer | int64 at offset 26 |
| `quantity` | base-10 integer | int64 at offset 34 |
| `order_id` | base-10 integer | uint64 at offset 42 |
| `trade_id` | base-10 integer | uint64 at offset 50 |

CSV numeric fields are parsed as integral base-10 text with no trailing characters.
CSV writers must emit the exact header above and canonical event/side spellings.

## Binary Format

Binary logs are little-endian. The header is 16 bytes:

| Offset | Width | Field | v1 value |
| --- | --- | --- | --- |
| 0 | 8 | magic ASCII | `ASTITCH1` |
| 8 | 2 | schema version | `1` |
| 10 | 2 | header size | `16` |
| 12 | 2 | record size | `58` |
| 14 | 2 | reserved | `0` |

Each following record is exactly 58 bytes with the field order listed above.
Readers reject unknown magic, unsupported schema versions, unexpected header/record
sizes, non-zero reserved header bytes, invalid enum wire values and truncated records.

## Enum Wire Values

`MarketEventType` values:

| Name | Wire |
| --- | --- |
| `Add` | 1 |
| `Cancel` | 2 |
| `Replace` | 3 |
| `Execute` | 4 |
| `Trade` | 5 |
| `Snapshot` | 6 |
| `Heartbeat` | 7 |

`Side` values:

| Name | Wire |
| --- | --- |
| `None` | 0 |
| `Buy` | 1 |
| `Sell` | 2 |

## Flags And Snapshots

The `flags` field is preserved in CSV, binary and event checksums.

Snapshot framing uses:

| Flag | Value | Meaning |
| --- | --- | --- |
| `kSnapshotBeginFlag` | `0x1` | Begin a snapshot block and reset the book |
| `kSnapshotEndFlag` | `0x2` | End a snapshot block |

A `Snapshot` event with `order_id == 0` is a pure marker. A `Snapshot` event with a
non-zero `order_id` reinstates one resting order using that record's side, price,
quantity, timestamp and sequence number.

Other flag bits have no replay semantics in schema v1. They are still carried through
round-trips and checksums, so assigning new semantics to them is a schema change.

## Compatibility Rules

Requires a version bump:

- binary magic, header layout, header size, record size or endianness changes;
- binary field order, field width, signedness or offset changes;
- `MarketEventType` or `Side` wire values change;
- CSV column names, required columns or column order change;
- canonical CSV event type or side spelling changes;
- checksum input order or checksum field set changes;
- snapshot marker flag semantics change;
- fixture formats stop round-tripping semantically through the current reader/writer.

Requires fixture regeneration:

- the writer output changes byte-for-byte for checked-in CSV/binary fixtures;
- the Binance normaliser changes canonical event mapping, synthetic order IDs or scale factors;
- sample input data changes;
- event checksums, replay checksums or expected fixture manifests change.

Does not require a version bump by itself:

- clearer error messages that keep rejecting the same malformed input;
- adding tests or documentation;
- adding new generated local files under ignored build directories.

## Migration Process

For an intentional breaking change:

1. Add the new manifest, for example `data/schema/event_log_schema_v2.json`.
2. Bump the binary schema version constant and update writer/reader validation.
3. Update this document with a short migration note: what changed, why, and how to
   convert old logs.
4. Regenerate checked-in fixtures and expected fixture manifests.
5. Update schema-drift tests so failures describe the new boundary.
6. Keep default CI dependency-light and network-free.

If old logs are still readable by the previous code, convert them before removing the
old reader:

```bash
PYTHONPATH=build/python python scripts/convert_event_log.py \
  --input old.csv --output migrated.bin --input-format csv --output-format binary
```

If old logs are no longer readable by the current code, write a one-off converter in
`tools/` or `scripts/` that explicitly names the old schema version. Do not silently
reinterpret old bytes as the new layout.

## Regenerating Fixtures

Regenerate binary samples from their CSV source after a schema or binary writer change:

```bash
PYTHONPATH=build/python python scripts/convert_event_log.py \
  --input data/samples/sample_replay.csv \
  --output data/samples/sample_replay.bin \
  --input-format csv --output-format binary

PYTHONPATH=build/python python scripts/convert_event_log.py \
  --input data/samples/sample_hot_path_replay.csv \
  --output data/samples/sample_hot_path_replay.bin \
  --input-format csv --output-format binary
```

Regenerate the Binance normalised fixture from the checked-in raw public-depth sample:

```bash
PYTHONPATH=build/python python tools/normalise_binance_depth_to_asterion.py \
  --input data/samples/binance_depth_sample.raw.jsonl \
  --csv-output data/samples/binance_depth_sample.normalised.csv \
  --binary-output data/samples/binance_depth_sample.normalised.bin \
  --json

PYTHONPATH=build/python python tools/normalise_binance_depth_to_asterion.py \
  --input data/samples/binance_depth_larger_sample.raw.jsonl \
  --csv-output data/samples/binance_depth_larger_sample.normalised.csv \
  --binary-output data/samples/binance_depth_larger_sample.normalised.bin \
  --json
```

Then update the corresponding `data/samples/binance_depth*_sample.expected.json`
manifest only after reviewing the normaliser report, event checksums, replay
checksums and grouped/shared parity fields.

## Interpreting Schema-Drift Failures

- `enum drift`: a C++ enum wire value, writer byte or mirrored Python constant moved.
- `CSV column drift`: the CSV header, required columns, column order or row width changed.
- `binary header drift`: binary magic/header fields changed or reserved bytes are non-zero.
- `binary layout drift`: record size, field offsets or field widths changed.
- `fixture checksum drift`: a checked-in fixture no longer matches the manifest expectation.
- `writer/reader semantic drift`: a known event stream no longer round-trips to the same
  event tuples/checksums.

When a failure is intentional, update the manifest, this document and affected fixtures
in the same commit so reviewers can see the migration boundary.
