from __future__ import annotations

from pathlib import Path

import pytest

# Requires the compiled extension; skipped when it is not built (e.g. offline).
asterion = pytest.importorskip("asterion")

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"


def _load(name: str):
    events = asterion.load_log(SAMPLES / name)
    assert events
    return events, events[0].symbol_id


@pytest.mark.parametrize(
    "name",
    ["sample_replay.csv", "binance_depth_sample.normalised.bin"],
)
def test_spsc_matches_single_thread(name: str) -> None:
    events, symbol_id = _load(name)
    baseline = asterion.run_replay(events, symbol_id=symbol_id)

    config = asterion.SpscReplayConfig()
    config.queue_capacity = 8
    spsc = asterion.run_spsc_replay(events, symbol_id, config)

    assert spsc.replay.events_processed == baseline.events_processed
    assert spsc.replay.final_book_checksum == baseline.final_book_checksum
    assert spsc.replay.execution_report_checksum == baseline.execution_report_checksum
    assert spsc.replay.diagnostics_checksum == baseline.diagnostics_checksum
    assert spsc.replay.sequence_valid == baseline.sequence_valid

    # Lossless by default: nothing dropped, every produced event consumed.
    assert spsc.stats.dropped_events == 0
    assert not spsc.stats.drop_policy_enabled
    assert spsc.stats.queue_capacity == 8
    if baseline.sequence_valid:
        assert spsc.stats.end_of_stream_seen
        assert spsc.stats.produced_events == spsc.stats.consumed_events == len(events)
        assert spsc.stats.max_queue_depth <= spsc.stats.queue_capacity


def test_spsc_tiny_queue_forces_backpressure_but_stays_lossless() -> None:
    events, symbol_id = _load("sample_replay.csv")
    config = asterion.SpscReplayConfig()
    config.queue_capacity = 1
    spsc = asterion.run_spsc_replay(events, symbol_id, config)

    baseline = asterion.run_replay(events, symbol_id=symbol_id)
    assert spsc.replay.final_book_checksum == baseline.final_book_checksum
    assert spsc.stats.dropped_events == 0
    assert spsc.stats.max_queue_depth <= 1
    assert spsc.stats.produced_events == spsc.stats.consumed_events


def test_spsc_is_deterministic_across_runs() -> None:
    events, symbol_id = _load("sample_replay.csv")
    config = asterion.SpscReplayConfig()
    config.queue_capacity = 2
    first = asterion.run_spsc_replay(events, symbol_id, config)
    for _ in range(20):
        again = asterion.run_spsc_replay(events, symbol_id, config)
        assert again.replay.final_book_checksum == first.replay.final_book_checksum
        assert again.replay.diagnostics_checksum == first.replay.diagnostics_checksum
        assert again.stats.dropped_events == 0
