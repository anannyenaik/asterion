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
NORMALISER = TOOLS / "normalise_binance_depth_to_asterion.py"

sys.path.insert(0, str(TOOLS))

import normalise_binance_depth_to_asterion as nb  # noqa: E402


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


def test_writer_is_byte_deterministic(tmp_path: Path) -> None:
    pytest.importorskip("asterion")
    result = nb.normalise_file(RAW)
    a_csv, a_bin = tmp_path / "a.csv", tmp_path / "a.bin"
    b_csv, b_bin = tmp_path / "b.csv", tmp_path / "b.bin"
    nb.write_event_logs(result.events, str(a_csv), str(a_bin))
    nb.write_event_logs(result.events, str(b_csv), str(b_bin))
    assert a_bin.read_bytes() == b_bin.read_bytes()
    assert a_csv.read_bytes() == b_csv.read_bytes()


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


def test_normaliser_event_codes_match_extension() -> None:
    pytest.importorskip("asterion")
    import asterion  # noqa: PLC0415

    assert nb.ET_ADD == int(asterion.MarketEventType.Add)
    assert nb.ET_CANCEL == int(asterion.MarketEventType.Cancel)
    assert nb.ET_REPLACE == int(asterion.MarketEventType.Replace)
    assert nb.ET_SNAPSHOT == int(asterion.MarketEventType.Snapshot)


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
