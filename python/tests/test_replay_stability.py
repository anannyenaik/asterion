from __future__ import annotations

from pathlib import Path

import pytest

# Requires the compiled extension; skipped when it is not built (e.g. offline).
asterion = pytest.importorskip("asterion")

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"


def test_replay_checksums_are_stable_across_runs() -> None:
    csv_path = SAMPLES / "sample_replay.csv"
    first = asterion.run_replay(csv_path, symbol_id=1)
    second = asterion.run_replay(csv_path, symbol_id=1)

    assert first.sequence_valid
    assert first.final_book_checksum == second.final_book_checksum
    assert first.execution_report_checksum == second.execution_report_checksum
    assert first.diagnostics_checksum == second.diagnostics_checksum
    assert first.event_log_checksum == second.event_log_checksum


def test_replay_checksums_match_across_formats() -> None:
    csv_result = asterion.run_replay(SAMPLES / "sample_replay.csv", symbol_id=1)
    bin_result = asterion.run_replay(SAMPLES / "sample_replay.bin", symbol_id=1)

    assert csv_result.final_book_checksum == bin_result.final_book_checksum
    assert csv_result.execution_report_checksum == bin_result.execution_report_checksum
    assert csv_result.diagnostics_checksum == bin_result.diagnostics_checksum
