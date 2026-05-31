"""Deterministic tests for the recorded Binance public-depth case study.

The parsing/diagnostics tests are pure-Python and always run. The CSV/binary
writer and replay tests require the compiled ``asterion`` extension and are
skipped when it is not built. No test touches the network.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"
TOOLS = ROOT / "tools"
RAW = SAMPLES / "binance_depth_sample.raw.jsonl"
NORM_CSV = SAMPLES / "binance_depth_sample.normalised.csv"
NORM_BIN = SAMPLES / "binance_depth_sample.normalised.bin"
EXPECTED = SAMPLES / "binance_depth_sample.expected.json"
NORMALISER = TOOLS / "normalise_binance_depth_to_asterion.py"

sys.path.insert(0, str(TOOLS))

import normalise_binance_depth_to_asterion as nb  # noqa: E402

EVENT_SCHEMA_DRIFT_HINT = (
    "normaliser's mirror constants must match the C++ event schema; update the "
    "pure-Python Binance normaliser constants, CSV writer expectations, binary "
    "writer expectations, fixtures, and expected manifest together if this "
    "schema drift is intentional."
)
EXPECTED_MARKET_EVENT_SCHEMA = {
    "Add": 1,
    "Cancel": 2,
    "Replace": 3,
    "Execute": 4,
    "Trade": 5,
    "Snapshot": 6,
    "Heartbeat": 7,
}
NORMALISER_EVENT_MIRRORS = {
    "Add": "ET_ADD",
    "Cancel": "ET_CANCEL",
    "Replace": "ET_REPLACE",
    "Snapshot": "ET_SNAPSHOT",
}
BINARY_EVENT_TYPE_OFFSET = 20


# --------------------------------------------------------------------------- #
# Pure-Python normalisation + diagnostics (no extension, no network)
# --------------------------------------------------------------------------- #


def test_fixture_normalises_to_expected_shape() -> None:
    result = nb.normalise_file(RAW)
    report = result.report()
    assert report["symbols"] == ["BTCUSDT"]
    assert report["symbol_ids"] == {"BTCUSDT": 1}
    assert report["raw_message_count"] == 5
    assert report["metadata_line_count"] == 1
    # snapshot block (begin + 4 levels + end) + Add,Add + Replace + Cancel,Cancel
    assert report["normalised_event_count"] == 11
    assert report["event_type_counts"] == {"snapshot": 6, "add": 2, "replace": 1, "cancel": 2}
    assert report["diagnostics_count"] == 0


def test_fixture_normalisation_is_deterministic() -> None:
    first = nb.normalise_file(RAW)
    second = nb.normalise_file(RAW)
    assert [vars(e) for e in first.events] == [vars(e) for e in second.events]
    assert first.events_checksum == second.events_checksum
    assert first.diagnostics_checksum == second.diagnostics_checksum


def test_synthetic_order_ids_are_unique_and_nonzero_for_levels() -> None:
    result = nb.normalise_file(RAW)
    level_order_ids = [e.order_id for e in result.events if e.order_id != 0]
    # Every Add/Replace/Cancel/level-Snapshot references a non-zero id; Adds and
    # snapshot levels must each introduce a unique id (no reuse -> clean replay).
    add_ids = [e.order_id for e in result.events if e.event_type in (nb.ET_ADD, nb.ET_SNAPSHOT) and e.order_id != 0]
    assert len(add_ids) == len(set(add_ids))
    assert all(oid > 0 for oid in level_order_ids)


def test_prices_and_quantities_scale_to_integer_ticks() -> None:
    result = nb.normalise_file(RAW)
    adds = [e for e in result.events if e.event_type == nb.ET_ADD]
    # First diff Add is bid 60000.50 @ 0.75 -> ticks at 1e8 scale.
    bid = next(e for e in adds if e.side == nb.SIDE_BUY)
    assert bid.price_ticks == 60000_50000000  # 60000.50 * 1e8
    assert bid.quantity == 75000000  # 0.75 * 1e8


@pytest.mark.parametrize(
    "lines,reason",
    [
        (["not json at all"], nb.REASON_MALFORMED_MESSAGE),
        (["[1, 2, 3]"], nb.REASON_MALFORMED_MESSAGE),
        (['{"e":"trade","s":"BTCUSDT","p":"1","q":"1"}'], nb.REASON_UNSUPPORTED_MESSAGE_TYPE),
        (['{"e":"depthUpdate","U":1,"u":1,"b":[["1","1"]],"a":[]}'], nb.REASON_MISSING_FIELDS),
        (
            [
                '{"e":"depthUpdate","E":1,"s":"X","U":1,"u":1,"b":[["100.00","1"]],"a":[]}',
                '{"e":"depthUpdate","E":2,"s":"X","U":3,"u":3,"b":[["101.00","1"]],"a":[]}',
            ],
            nb.REASON_UPDATE_GAP,
        ),
        (
            [
                '{"e":"depthUpdate","E":1,"s":"X","U":5,"u":5,"b":[["100.00","1"]],"a":[]}',
                '{"e":"depthUpdate","E":2,"s":"X","U":2,"u":3,"b":[["101.00","1"]],"a":[]}',
            ],
            nb.REASON_STALE_UPDATE,
        ),
        (
            [
                '{"e":"depthUpdate","E":2000,"s":"X","U":1,"u":1,"b":[["100.00","1"]],"a":[]}',
                '{"e":"depthUpdate","E":1000,"s":"X","U":2,"u":2,"b":[["101.00","1"]],"a":[]}',
            ],
            nb.REASON_NON_MONOTONIC_EVENT_TIME,
        ),
        (['{"e":"depthUpdate","E":1,"s":"X","U":1,"u":1,"b":[["100.00","1"]],"a":[["99.00","1"]]}'], nb.REASON_CROSSED_BOOK),
        (['{"e":"depthUpdate","E":1,"s":"X","U":1,"u":1,"b":[["-5.00","1"]],"a":[]}'], nb.REASON_INVALID_PRICE),
        (['{"e":"depthUpdate","E":1,"s":"X","U":1,"u":1,"b":[["100.00","-1"]],"a":[]}'], nb.REASON_INVALID_QUANTITY),
        (['{"e":"depthUpdate","E":1,"s":"X","U":1,"u":1,"b":[["100.00","0"]],"a":[]}'], nb.REASON_LEVEL_REMOVE_ABSENT),
    ],
)
def test_diagnostics_fire_for_bad_input(lines: list[str], reason: str) -> None:
    result = nb.normalise_lines(lines)
    assert reason in result.diagnostics_by_reason, result.diagnostics_by_reason


def test_diagnostics_are_deterministic_for_bad_input() -> None:
    lines = ["not json", '{"e":"depthUpdate","U":1,"u":1,"b":[["1","1"]],"a":[]}']
    first = nb.normalise_lines(lines)
    second = nb.normalise_lines(lines)
    assert first.diagnostics_checksum == second.diagnostics_checksum
    assert first.diagnostics_by_reason == second.diagnostics_by_reason


def test_capture_module_imports_without_network() -> None:
    # Importing the capture tool must not perform any network I/O.
    sys.path.insert(0, str(TOOLS))
    import capture_binance_depth as cap  # noqa: PLC0415

    assert cap.STREAM_TYPE == "rest_depth_snapshot_poll"
    assert cap.TOOL_VERSION


# --------------------------------------------------------------------------- #
# CLI behaviour (report JSON needs no extension)
# --------------------------------------------------------------------------- #


def test_cli_reports_json_without_extension() -> None:
    result = subprocess.run(
        [sys.executable, str(NORMALISER), "--input", str(RAW), "--json"],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["normalised_event_count"] == 11
    assert payload["diagnostics_count"] == 0
    assert payload["symbols"] == ["BTCUSDT"]


# --------------------------------------------------------------------------- #
# CSV/binary writer + replay (require the compiled extension)
# --------------------------------------------------------------------------- #


def _evt_tuple(event: object) -> tuple:
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


def _norm_tuple(event: "nb.NormEvent") -> tuple:
    return (
        event.timestamp_ns,
        event.sequence_number,
        event.symbol_id,
        event.event_type,
        event.side,
        event.price_ticks,
        event.quantity,
        event.order_id,
        event.trade_id,
        event.flags,
    )


def _manifest() -> dict:
    return json.loads(EXPECTED.read_text(encoding="utf-8"))


def _guard_equal(property_name: str, actual: object, expected: object) -> None:
    assert actual == expected, (
        f"Binance fixture regeneration guard drifted for {property_name}: "
        f"expected {expected!r}, got {actual!r}. If intentional, regenerate "
        f"{NORM_CSV.relative_to(ROOT)} and {NORM_BIN.relative_to(ROOT)} from "
        f"{RAW.relative_to(ROOT)}, then update {EXPECTED.relative_to(ROOT)}."
    )


def _read_event_log_checked(asterion: object, path: Path, format_name: str):
    result = asterion.read_event_log(path, asterion.parse_event_log_format(format_name))
    assert not result.error, f"failed to read {path}: {result.error}"
    return result


def _assert_csv_lines_match(generated: Path, expected: Path) -> None:
    generated_lines = generated.read_text(encoding="utf-8").splitlines()
    expected_lines = expected.read_text(encoding="utf-8").splitlines()
    if generated_lines == expected_lines:
        return

    max_len = max(len(generated_lines), len(expected_lines))
    for index in range(max_len):
        generated_line = generated_lines[index] if index < len(generated_lines) else "<missing>"
        expected_line = expected_lines[index] if index < len(expected_lines) else "<missing>"
        if generated_line != expected_line:
            pytest.fail(
                "Binance fixture regeneration guard drifted for normalised CSV "
                f"line {index + 1}: expected {expected_line!r}, got {generated_line!r}. "
                f"Regenerate {expected.relative_to(ROOT)} only if the normaliser "
                "change is intentional."
            )


def test_writer_is_byte_deterministic(tmp_path: Path) -> None:
    pytest.importorskip("asterion")
    result = nb.normalise_file(RAW)
    a_csv, a_bin = tmp_path / "a.csv", tmp_path / "a.bin"
    b_csv, b_bin = tmp_path / "b.csv", tmp_path / "b.bin"
    nb.write_event_logs(result.events, str(a_csv), str(a_bin))
    nb.write_event_logs(result.events, str(b_csv), str(b_bin))
    assert a_bin.read_bytes() == b_bin.read_bytes()
    assert a_csv.read_bytes() == b_csv.read_bytes()


def test_fixture_regeneration_guard_matches_expected_manifest(tmp_path: Path) -> None:
    pytest.importorskip("asterion")
    import asterion  # noqa: PLC0415

    expected = _manifest()
    result = nb.normalise_file(RAW)
    report = result.report(source=expected["source"])

    _guard_equal("schema_version", expected["schema_version"], 1)
    _guard_equal("source", expected["source"], str(RAW.relative_to(ROOT)).replace("\\", "/"))
    _guard_equal("normalised_csv", expected["normalised_csv"], str(NORM_CSV.relative_to(ROOT)).replace("\\", "/"))
    _guard_equal("normalised_binary", expected["normalised_binary"], str(NORM_BIN.relative_to(ROOT)).replace("\\", "/"))
    _guard_equal("normaliser", expected["normaliser"], str(NORMALISER.relative_to(ROOT)).replace("\\", "/"))
    _guard_equal("tool_version", report["tool_version"], expected["tool_version"])
    _guard_equal("raw_line_count", report["raw_line_count"], expected["raw_line_count"])
    _guard_equal("raw_message_count", report["raw_message_count"], expected["raw_message_count"])
    _guard_equal("metadata_line_count", report["metadata_line_count"], expected["metadata_line_count"])
    _guard_equal("normalised_event_count", report["normalised_event_count"], expected["normalised_event_count"])
    _guard_equal("event_type_counts", report["event_type_counts"], expected["event_type_counts"])
    _guard_equal("symbols", report["symbols"], expected["symbols"])
    _guard_equal("symbol_ids", report["symbol_ids"], expected["symbol_ids"])
    _guard_equal("normaliser_events_checksum", report["events_checksum"], expected["normaliser_events_checksum"])
    _guard_equal(
        "normaliser_diagnostics_checksum",
        report["diagnostics_checksum"],
        expected["normaliser_diagnostics_checksum"],
    )
    _guard_equal("diagnostics_count", report["diagnostics_count"], expected["diagnostics_count"])

    generated_csv = tmp_path / "binance_depth_sample.normalised.csv"
    generated_bin = tmp_path / "binance_depth_sample.normalised.bin"
    nb.write_event_logs(result.events, str(generated_csv), str(generated_bin))

    _assert_csv_lines_match(generated_csv, NORM_CSV)

    expected_tuples = [_norm_tuple(e) for e in result.events]
    csv_read = _read_event_log_checked(asterion, generated_csv, "csv")
    binary_read = _read_event_log_checked(asterion, generated_bin, "binary")
    committed_csv_read = _read_event_log_checked(asterion, NORM_CSV, "csv")
    committed_binary_read = _read_event_log_checked(asterion, NORM_BIN, "binary")

    csv_tuples = [_evt_tuple(e) for e in csv_read.events]
    binary_tuples = [_evt_tuple(e) for e in binary_read.events]
    committed_csv_tuples = [_evt_tuple(e) for e in committed_csv_read.events]
    committed_binary_tuples = [_evt_tuple(e) for e in committed_binary_read.events]

    _guard_equal("generated CSV event tuples", csv_tuples, expected_tuples)
    _guard_equal("generated binary event tuples", binary_tuples, expected_tuples)
    _guard_equal("CSV/binary event tuple equivalence", binary_tuples, csv_tuples)
    _guard_equal("committed CSV event tuples", committed_csv_tuples, expected_tuples)
    _guard_equal("committed binary event tuples", committed_binary_tuples, expected_tuples)
    _guard_equal("committed CSV/binary event tuple equivalence", committed_binary_tuples, committed_csv_tuples)
    _guard_equal("csv_event_checksum", csv_read.event_checksum, expected["csv_event_checksum"])
    _guard_equal("binary_event_checksum", binary_read.event_checksum, expected["binary_event_checksum"])
    _guard_equal(
        "committed_csv_event_checksum",
        committed_csv_read.event_checksum,
        expected["committed_csv_event_checksum"],
    )
    _guard_equal(
        "committed_binary_event_checksum",
        committed_binary_read.event_checksum,
        expected["committed_binary_event_checksum"],
    )

    binary_bytes = generated_bin.read_bytes()
    header = binary_bytes[: expected["binary_header_size"]]
    header_size = int.from_bytes(header[10:12], "little")
    record_size = int.from_bytes(header[12:14], "little")
    payload_size = len(binary_bytes) - header_size
    _guard_equal("binary_magic", header[:8].decode("ascii"), expected["binary_magic"])
    _guard_equal("binary_version", int.from_bytes(header[8:10], "little"), expected["binary_version"])
    _guard_equal("binary_header_size", header_size, expected["binary_header_size"])
    _guard_equal("binary_record_size", record_size, expected["binary_record_size"])
    _guard_equal("binary_payload_remainder", payload_size % record_size, 0)
    _guard_equal("binary_record_count", payload_size // record_size, expected["binary_record_count"])

    replay_symbol = expected["replay"]["symbol_id"]
    csv_replay = asterion.run_replay(generated_csv, symbol_id=replay_symbol, format="csv")
    binary_replay = asterion.run_replay(generated_bin, symbol_id=replay_symbol, format="binary")
    replay_fields = (
        "events_processed",
        "sequence_valid",
        "event_log_checksum",
        "final_book_checksum",
        "execution_report_checksum",
        "diagnostics_checksum",
        "diagnostic_error_count",
        "diagnostic_warning_count",
    )
    for field in replay_fields:
        _guard_equal(f"CSV/binary replay {field}", getattr(csv_replay, field), getattr(binary_replay, field))
        _guard_equal(f"replay.{field}", getattr(binary_replay, field), expected["replay"][field])
    _guard_equal("replay.error", binary_replay.error, "")


def test_committed_fixtures_match_current_normaliser() -> None:
    pytest.importorskip("asterion")
    import asterion  # noqa: PLC0415

    if not (NORM_CSV.exists() and NORM_BIN.exists()):
        pytest.skip("committed normalised fixtures not present")

    expected = [_norm_tuple(e) for e in nb.normalise_file(RAW).events]
    csv_events = [_evt_tuple(e) for e in asterion.load_log(NORM_CSV)]
    bin_events = [_evt_tuple(e) for e in asterion.load_log(NORM_BIN)]
    assert csv_events == expected
    assert bin_events == expected


def test_event_schema_codes_match_normaliser_and_writers(tmp_path: Path) -> None:
    pytest.importorskip("asterion")
    import asterion  # noqa: PLC0415

    events = []
    expected_values = []
    for sequence, (name, expected_value) in enumerate(EXPECTED_MARKET_EVENT_SCHEMA.items(), start=1):
        extension_value = getattr(asterion.MarketEventType, name)
        assert int(extension_value) == expected_value, (
            f"{EVENT_SCHEMA_DRIFT_HINT} MarketEventType.{name} expected "
            f"{expected_value}, got {int(extension_value)}."
        )
        assert asterion.market_event_type_to_string(extension_value) == name, (
            f"{EVENT_SCHEMA_DRIFT_HINT} CSV writer string for MarketEventType.{name} "
            f"drifted from {name!r}."
        )
        csv_line = asterion.market_data_event_to_csv(
            asterion.MarketDataEvent(
                timestamp_ns=sequence,
                sequence_number=sequence,
                symbol_id=1,
                event_type=extension_value,
                side=asterion.Side.Buy,
                price_ticks=100 + sequence,
                quantity=10 + sequence,
                order_id=sequence,
                trade_id=0,
                flags=0,
            )
        )
        assert csv_line.split(",")[3] == name, (
            f"{EVENT_SCHEMA_DRIFT_HINT} CSV writer event_type field for "
            f"MarketEventType.{name} drifted: {csv_line!r}."
        )
        events.append(
            asterion.MarketDataEvent(
                timestamp_ns=sequence,
                sequence_number=sequence,
                symbol_id=1,
                event_type=extension_value,
                side=asterion.Side.Buy,
                price_ticks=100 + sequence,
                quantity=10 + sequence,
                order_id=sequence,
                trade_id=0,
                flags=0,
            )
        )
        expected_values.append(expected_value)

    for name, attr in NORMALISER_EVENT_MIRRORS.items():
        actual = getattr(nb, attr)
        expected = EXPECTED_MARKET_EVENT_SCHEMA[name]
        assert actual == expected, (
            f"{EVENT_SCHEMA_DRIFT_HINT} {attr} mirrors MarketEventType.{name}; "
            f"expected {expected}, got {actual}."
        )

    csv_path = tmp_path / "schema.csv"
    binary_path = tmp_path / "schema.bin"
    asterion.write_log(csv_path, events, "csv")
    asterion.write_log(binary_path, events, "binary")
    csv_values = [int(e.event_type) for e in asterion.load_log(csv_path, "csv")]
    binary_values = [int(e.event_type) for e in asterion.load_log(binary_path, "binary")]
    assert csv_values == expected_values, (
        f"{EVENT_SCHEMA_DRIFT_HINT} CSV writer/reader event-type values drifted: "
        f"expected {expected_values!r}, got {csv_values!r}."
    )
    assert binary_values == expected_values, (
        f"{EVENT_SCHEMA_DRIFT_HINT} binary writer/reader event-type values drifted: "
        f"expected {expected_values!r}, got {binary_values!r}."
    )

    binary_bytes = binary_path.read_bytes()
    header_size = int.from_bytes(binary_bytes[10:12], "little")
    record_size = int.from_bytes(binary_bytes[12:14], "little")
    wire_values = [
        binary_bytes[header_size + index * record_size + BINARY_EVENT_TYPE_OFFSET]
        for index in range(len(expected_values))
    ]
    assert wire_values == expected_values, (
        f"{EVENT_SCHEMA_DRIFT_HINT} binary writer wire event-type bytes drifted: "
        f"expected {expected_values!r}, got {wire_values!r}."
    )


def test_csv_binary_equivalence_and_replay_checksums() -> None:
    pytest.importorskip("asterion")
    import asterion  # noqa: PLC0415

    if not (NORM_CSV.exists() and NORM_BIN.exists()):
        pytest.skip("committed normalised fixtures not present")

    # CSV/binary equivalence.
    assert [_evt_tuple(e) for e in asterion.load_log(NORM_CSV)] == [
        _evt_tuple(e) for e in asterion.load_log(NORM_BIN)
    ]

    # Replay checksum stability + cross-format agreement.
    csv1 = asterion.run_replay(NORM_CSV, symbol_id=1)
    csv2 = asterion.run_replay(NORM_CSV, symbol_id=1)
    binr = asterion.run_replay(NORM_BIN, symbol_id=1)

    assert csv1.sequence_valid
    assert csv1.final_book_checksum == csv2.final_book_checksum == binr.final_book_checksum
    assert csv1.execution_report_checksum == binr.execution_report_checksum
    assert csv1.diagnostics_checksum == binr.diagnostics_checksum
    # The clean fixture must replay without error diagnostics.
    assert binr.diagnostic_error_count == 0


def test_replay_parity_grouped_vs_shared() -> None:
    pytest.importorskip("asterion")
    import asterion  # noqa: PLC0415

    if not NORM_CSV.exists():
        pytest.skip("committed normalised fixtures not present")
    grouped = asterion.aggregate_by_symbol(NORM_CSV)
    shared = asterion.aggregate_by_symbol(NORM_CSV, shared=True)
    assert grouped.aggregate_checksum == shared.aggregate_checksum
    assert grouped.combined_book_checksum == shared.combined_book_checksum
    assert grouped.symbol_count == shared.symbol_count == 1
