from __future__ import annotations

import json
import sys
from pathlib import Path

import pytest

asterion = pytest.importorskip("asterion")

ROOT = Path(__file__).resolve().parents[2]
MANIFEST = ROOT / "data" / "schema" / "event_log_schema_v1.json"
TOOLS_AND_SCRIPTS = (ROOT / "scripts", ROOT / "tools")

SCHEMA_DRIFT_HINT = (
    "If this event-log schema drift is intentional, update "
    "data/schema/event_log_schema_v1.json and docs/event_log_schema.md with the "
    "migration note, then regenerate affected fixtures."
)


def _schema() -> dict:
    return json.loads(MANIFEST.read_text(encoding="utf-8"))


def _event_tuple(event: object) -> tuple:
    return (
        event.timestamp_ns,
        event.sequence_number,
        event.symbol_id,
        int(event.event_type),
        int(event.side),
        event.price_ticks,
        event.quantity,
        event.order_id,
        event.trade_id,
        event.flags,
    )


def _read_checked(path: Path, format_name: str):
    result = asterion.read_event_log(path, asterion.parse_event_log_format(format_name))
    assert not result.error, f"writer/reader semantic drift while reading {path}: {result.error}"
    return result


def _write_checked(path: Path, events: list[object], format_name: str):
    result = asterion.write_log(path, events, format_name)
    assert not result.error, f"writer/reader semantic drift while writing {path}: {result.error}"
    return result


def _write_bytes(path: Path, payload: bytes | bytearray) -> None:
    path.write_bytes(bytes(payload))


def _assert_error_contains(path: Path, format_name: str, *needles: str) -> None:
    result = asterion.read_event_log(path, asterion.parse_event_log_format(format_name))
    assert result.error, f"malformed {format_name} log was accepted accidentally: {path}"
    for needle in needles:
        assert needle in result.error, (
            f"expected {format_name} error to mention {needle!r}, got {result.error!r}. "
            f"{SCHEMA_DRIFT_HINT}"
        )
    assert not result.events


def _sample_event(event_type=asterion.MarketEventType.Add, side=asterion.Side.Buy):
    return asterion.MarketDataEvent(
        timestamp_ns=1,
        sequence_number=1,
        symbol_id=1,
        event_type=event_type,
        side=side,
        price_ticks=1000,
        quantity=10,
        order_id=1,
        trade_id=0,
        flags=0,
    )


def test_event_log_schema_manifest_is_documented() -> None:
    schema = _schema()
    assert schema["schema_version"] == 1, (
        "event-log schema version drifted from v1. "
        f"{SCHEMA_DRIFT_HINT}"
    )
    doc = ROOT / schema["migration_documentation"]
    assert doc.exists(), f"missing event-log migration documentation: {doc}. {SCHEMA_DRIFT_HINT}"
    text = doc.read_text(encoding="utf-8")
    for phrase in (
        "Event-Log Schema v1",
        "data/schema/event_log_schema_v1.json",
        "version bump",
        "fixture regeneration",
        "schema-drift",
    ):
        assert phrase in text, (
            f"event-log migration documentation is missing {phrase!r}. {SCHEMA_DRIFT_HINT}"
        )


def test_csv_schema_manifest_matches_writer_and_parser(tmp_path: Path) -> None:
    schema = _schema()
    csv_schema = schema["csv"]
    assert csv_schema["columns"] == csv_schema["required_columns"], (
        f"CSV required-column manifest drifted. {SCHEMA_DRIFT_HINT}"
    )
    assert ",".join(csv_schema["columns"]) == csv_schema["header"], (
        f"CSV column-order manifest drifted. {SCHEMA_DRIFT_HINT}"
    )

    events = []
    expected_names = list(schema["enums"]["MarketEventType"])
    for index, name in enumerate(expected_names, start=1):
        event_type = getattr(asterion.MarketEventType, name)
        events.append(
            asterion.MarketDataEvent(
                timestamp_ns=index,
                sequence_number=index,
                symbol_id=1,
                event_type=event_type,
                side=asterion.Side.Buy,
                price_ticks=1000 + index,
                quantity=10 + index,
                order_id=index,
                trade_id=0,
                flags=0,
            )
        )

    csv_path = tmp_path / "schema.csv"
    _write_checked(csv_path, events, "csv")
    lines = csv_path.read_text(encoding="utf-8").splitlines()
    assert lines[0] == csv_schema["header"], (
        "CSV column drift: writer header no longer matches the schema manifest. "
        f"{SCHEMA_DRIFT_HINT}"
    )
    assert [line.split(",")[3] for line in lines[1:]] == expected_names, (
        "CSV event type spelling drifted from the schema manifest. "
        f"{SCHEMA_DRIFT_HINT}"
    )

    read = _read_checked(csv_path, "csv")
    assert [_event_tuple(event) for event in read.events] == [_event_tuple(event) for event in events], (
        "writer/reader semantic drift: CSV output no longer round-trips event tuples. "
        f"{SCHEMA_DRIFT_HINT}"
    )


def test_csv_schema_rejects_column_and_numeric_drift(tmp_path: Path) -> None:
    header = _schema()["csv"]["header"]

    reordered = tmp_path / "reordered.csv"
    reordered.write_text(
        "event_type,timestamp_ns,sequence_number,symbol_id,side,price_ticks,quantity,"
        "order_id,trade_id,flags\n",
        encoding="utf-8",
    )
    _assert_error_contains(reordered, "csv", "CSV column drift")

    missing_required = tmp_path / "missing_required.csv"
    missing_required.write_text(
        "timestamp_ns,sequence_number,symbol_id,event_type,side,price_ticks,quantity,"
        "order_id,trade_id\n",
        encoding="utf-8",
    )
    _assert_error_contains(missing_required, "csv", "CSV column drift")

    decimal_timestamp = tmp_path / "decimal_timestamp.csv"
    decimal_timestamp.write_text(
        f"{header}\n1.5,1,1,Add,Buy,1000,10,1,0,0\n",
        encoding="utf-8",
    )
    _assert_error_contains(decimal_timestamp, "csv", "numeric", "timestamp_ns")

    bad_event_spelling = tmp_path / "bad_event_spelling.csv"
    bad_event_spelling.write_text(
        f"{header}\n1,1,1,add,Buy,1000,10,1,0,0\n",
        encoding="utf-8",
    )
    _assert_error_contains(bad_event_spelling, "csv", "event_type spelling", "event_type")


def test_binary_schema_manifest_matches_header_record_layout_and_enums(tmp_path: Path) -> None:
    schema = _schema()
    binary = schema["binary"]
    event_values = schema["enums"]["MarketEventType"]
    side_values = schema["enums"]["Side"]

    for name, expected in event_values.items():
        actual = int(getattr(asterion.MarketEventType, name))
        assert actual == expected, (
            f"enum drift: MarketEventType.{name} expected wire value {expected}, got {actual}. "
            f"{SCHEMA_DRIFT_HINT}"
        )
        assert asterion.market_event_type_to_string(getattr(asterion.MarketEventType, name)) == name, (
            f"CSV event type spelling drifted for MarketEventType.{name}. {SCHEMA_DRIFT_HINT}"
        )

    for name, expected in side_values.items():
        attr = "None_" if name == "None" else name
        actual = int(getattr(asterion.Side, attr))
        assert actual == expected, (
            f"enum drift: Side.{name} expected wire value {expected}, got {actual}. "
            f"{SCHEMA_DRIFT_HINT}"
        )

    sentinel_values = {
        "timestamp_ns": 0x0102030405060708,
        "sequence_number": 0x1112131415161718,
        "symbol_id": 0x21222324,
        "event_type": event_values["Trade"],
        "side": side_values["Sell"],
        "flags": 0x31323334,
        "price_ticks": 0x0414243444546474,
        "quantity": 0x0515253545556575,
        "order_id": 0x6162636465666768,
        "trade_id": 0x7172737475767778,
    }
    event = asterion.MarketDataEvent(
        timestamp_ns=sentinel_values["timestamp_ns"],
        sequence_number=sentinel_values["sequence_number"],
        symbol_id=sentinel_values["symbol_id"],
        event_type=asterion.MarketEventType.Trade,
        side=asterion.Side.Sell,
        price_ticks=sentinel_values["price_ticks"],
        quantity=sentinel_values["quantity"],
        order_id=sentinel_values["order_id"],
        trade_id=sentinel_values["trade_id"],
        flags=sentinel_values["flags"],
    )
    binary_path = tmp_path / "schema.bin"
    _write_checked(binary_path, [event], "binary")
    payload = binary_path.read_bytes()
    assert len(payload) == binary["header_size_bytes"] + binary["record_size_bytes"], (
        "binary layout drift: one-record file size no longer matches header + record size. "
        f"{SCHEMA_DRIFT_HINT}"
    )

    for field in binary["header_layout"]:
        raw = payload[field["offset"] : field["offset"] + field["width_bytes"]]
        if field.get("encoding") == "ascii":
            actual = raw.decode("ascii")
        else:
            actual = int.from_bytes(raw, binary["endianness"], signed=False)
        assert actual == field["value"], (
            f"binary header drift: {field['name']} expected {field['value']!r}, got {actual!r}. "
            f"{SCHEMA_DRIFT_HINT}"
        )

    record_start = binary["header_size_bytes"]
    for field in binary["record_layout"]:
        raw = payload[record_start + field["offset"] : record_start + field["offset"] + field["width_bytes"]]
        actual = int.from_bytes(raw, binary["endianness"], signed=field.get("signed", False))
        expected = sentinel_values[field["name"]]
        assert actual == expected, (
            f"binary layout drift: field {field['name']} at offset {field['offset']} "
            f"expected {expected!r}, got {actual!r}. {SCHEMA_DRIFT_HINT}"
        )


def test_binary_schema_rejects_malformed_headers_records_and_enums(tmp_path: Path) -> None:
    schema = _schema()
    binary = schema["binary"]
    path = tmp_path / "valid.bin"
    _write_checked(path, [_sample_event()], "binary")
    payload = bytearray(path.read_bytes())
    header_size = binary["header_size_bytes"]
    record_size = binary["record_size_bytes"]
    event_type_offset = next(field["offset"] for field in binary["record_layout"] if field["name"] == "event_type")
    side_offset = next(field["offset"] for field in binary["record_layout"] if field["name"] == "side")

    truncated_header = tmp_path / "truncated_header.bin"
    _write_bytes(truncated_header, payload[:3])
    _assert_error_contains(truncated_header, "binary", "malformed binary event log", "truncated")

    bad_magic = tmp_path / "bad_magic.bin"
    changed = bytearray(payload)
    changed[0:8] = b"BADITCH1"
    _write_bytes(bad_magic, changed)
    _assert_error_contains(bad_magic, "binary", "binary header drift", "magic")

    bad_version = tmp_path / "bad_version.bin"
    changed = bytearray(payload)
    changed[8:10] = (99).to_bytes(2, "little")
    _write_bytes(bad_version, changed)
    _assert_error_contains(bad_version, "binary", "binary schema version drift", "version")

    bad_record_size = tmp_path / "bad_record_size.bin"
    changed = bytearray(payload)
    changed[12:14] = (record_size + 1).to_bytes(2, "little")
    _write_bytes(bad_record_size, changed)
    _assert_error_contains(bad_record_size, "binary", "binary layout drift", "record size")

    bad_reserved = tmp_path / "bad_reserved.bin"
    changed = bytearray(payload)
    changed[14:16] = (1).to_bytes(2, "little")
    _write_bytes(bad_reserved, changed)
    _assert_error_contains(bad_reserved, "binary", "binary header drift", "reserved header field")

    truncated_record = tmp_path / "truncated_record.bin"
    _write_bytes(truncated_record, payload[: header_size + record_size - 1])
    _assert_error_contains(truncated_record, "binary", "malformed binary event log", "truncated")

    bad_event = tmp_path / "bad_event.bin"
    changed = bytearray(payload)
    changed[header_size + event_type_offset] = 99
    _write_bytes(bad_event, changed)
    _assert_error_contains(bad_event, "binary", "enum drift", "MarketEventType")

    bad_side = tmp_path / "bad_side.bin"
    changed = bytearray(payload)
    changed[header_size + side_offset] = 99
    _write_bytes(bad_side, changed)
    _assert_error_contains(bad_side, "binary", "enum drift", "Side")


def test_schema_manifest_fixture_checksums_and_semantic_roundtrips(tmp_path: Path) -> None:
    schema = _schema()
    by_path: dict[str, list[tuple]] = {}
    for fixture in schema["fixtures"]:
        path = ROOT / fixture["path"]
        result = _read_checked(path, fixture["format"])
        assert len(result.events) == fixture["event_count"], (
            f"fixture checksum drift: {fixture['path']} event count expected "
            f"{fixture['event_count']}, got {len(result.events)}. {SCHEMA_DRIFT_HINT}"
        )
        assert result.event_checksum == fixture["event_checksum"], (
            f"fixture checksum drift: {fixture['path']} expected event checksum "
            f"{fixture['event_checksum']}, got {result.event_checksum}. {SCHEMA_DRIFT_HINT}"
        )
        by_path[fixture["path"]] = [_event_tuple(event) for event in result.events]

        if fixture["format"] == "csv":
            header = path.read_text(encoding="utf-8").splitlines()[0]
            assert header == schema["csv"]["header"], (
                f"CSV column drift in fixture {fixture['path']}. {SCHEMA_DRIFT_HINT}"
            )
        else:
            payload = path.read_bytes()
            header_size = schema["binary"]["header_size_bytes"]
            record_size = schema["binary"]["record_size_bytes"]
            assert payload[:8].decode("ascii") == schema["binary"]["magic_ascii"], (
                f"binary header drift in fixture {fixture['path']}. {SCHEMA_DRIFT_HINT}"
            )
            assert int.from_bytes(payload[8:10], "little") == schema["schema_version"], (
                f"binary schema version drift in fixture {fixture['path']}. {SCHEMA_DRIFT_HINT}"
            )
            assert int.from_bytes(payload[10:12], "little") == header_size
            assert int.from_bytes(payload[12:14], "little") == record_size
            assert (len(payload) - header_size) % record_size == 0, (
                f"binary layout drift in fixture {fixture['path']}: payload is not a whole "
                f"number of records. {SCHEMA_DRIFT_HINT}"
            )

    for fixture in schema["fixtures"]:
        if fixture["format"] != "csv":
            continue
        source = ROOT / fixture["path"]
        source_read = _read_checked(source, "csv")
        csv_roundtrip = tmp_path / source.name
        bin_roundtrip = tmp_path / source.with_suffix(".bin").name
        _write_checked(csv_roundtrip, source_read.events, "csv")
        _write_checked(bin_roundtrip, source_read.events, "binary")
        assert [_event_tuple(event) for event in _read_checked(csv_roundtrip, "csv").events] == by_path[
            fixture["path"]
        ], f"writer/reader semantic drift in CSV round-trip for {fixture['path']}. {SCHEMA_DRIFT_HINT}"
        assert [_event_tuple(event) for event in _read_checked(bin_roundtrip, "binary").events] == by_path[
            fixture["path"]
        ], f"writer/reader semantic drift in CSV-to-binary round-trip for {fixture['path']}. {SCHEMA_DRIFT_HINT}"


def test_schema_manifest_matches_python_generators() -> None:
    schema = _schema()
    for path in TOOLS_AND_SCRIPTS:
        sys.path.insert(0, str(path))
    import generate_synthetic_events as generator  # noqa: PLC0415
    import normalise_binance_depth_to_asterion as binance  # noqa: PLC0415

    assert generator.CSV_HEADER == schema["csv"]["columns"], (
        f"CSV column drift in scripts/generate_synthetic_events.py. {SCHEMA_DRIFT_HINT}"
    )
    assert generator.BINARY_MAGIC.decode("ascii") == schema["binary"]["magic_ascii"], (
        f"binary header drift in scripts/generate_synthetic_events.py. {SCHEMA_DRIFT_HINT}"
    )
    assert generator.BINARY_VERSION == schema["schema_version"], (
        f"binary schema version drift in scripts/generate_synthetic_events.py. {SCHEMA_DRIFT_HINT}"
    )
    assert generator.BINARY_HEADER_SIZE == schema["binary"]["header_size_bytes"]
    assert generator.BINARY_RECORD_SIZE == schema["binary"]["record_size_bytes"]
    assert generator.BINARY_RECORD.size == schema["binary"]["record_size_bytes"]
    assert generator.EVENT_TYPE_IDS == schema["enums"]["MarketEventType"], (
        f"enum drift in scripts/generate_synthetic_events.py. {SCHEMA_DRIFT_HINT}"
    )
    assert generator.SIDE_IDS == schema["enums"]["Side"], (
        f"enum drift in scripts/generate_synthetic_events.py. {SCHEMA_DRIFT_HINT}"
    )

    assert binance.ET_ADD == schema["enums"]["MarketEventType"]["Add"]
    assert binance.ET_CANCEL == schema["enums"]["MarketEventType"]["Cancel"]
    assert binance.ET_REPLACE == schema["enums"]["MarketEventType"]["Replace"]
    assert binance.ET_SNAPSHOT == schema["enums"]["MarketEventType"]["Snapshot"]
    assert binance.SIDE_NONE == schema["enums"]["Side"]["None"]
    assert binance.SIDE_BUY == schema["enums"]["Side"]["Buy"]
    assert binance.SIDE_SELL == schema["enums"]["Side"]["Sell"]
