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
        assert spsc.stats.end_of_stream_markers_produced == 1
        assert spsc.stats.end_of_stream_markers_consumed == 1
        assert spsc.stats.produced_events == spsc.stats.consumed_events == len(events)
        assert spsc.stats.max_queue_depth <= spsc.stats.queue_capacity
        assert spsc.stats.elapsed_ns > 0
        assert spsc.stats.throughput_events_per_second > 0.0


@pytest.mark.parametrize(
    "name",
    ["sample_replay.csv", "binance_depth_sample.normalised.bin"],
)
def test_steady_state_spsc_matches_single_thread_with_light_validation(name: str) -> None:
    events, symbol_id = _load(name)
    replay_config = asterion.ReplayConfig()
    replay_config.validation_mode = asterion.ReplayValidationMode.Light
    baseline = asterion.run_replay(events, symbol_id=symbol_id, config=replay_config)

    config = asterion.SpscReplayConfig()
    config.queue_capacity = 8
    config.replay = replay_config
    spsc = asterion.run_spsc_replay_steady_state(events, symbol_id, config)

    assert spsc.replay.events_processed == baseline.events_processed
    assert spsc.replay.final_book_checksum == baseline.final_book_checksum
    assert spsc.replay.execution_report_checksum == baseline.execution_report_checksum
    assert spsc.replay.diagnostics_checksum == baseline.diagnostics_checksum
    assert spsc.replay.sequence_valid == baseline.sequence_valid

    assert spsc.stats.dropped_events == 0
    assert spsc.stats.end_of_stream_seen
    assert spsc.stats.end_of_stream_markers_produced == 1
    assert spsc.stats.end_of_stream_markers_consumed == 1
    assert spsc.stats.produced_events == spsc.stats.consumed_events == len(events)
    assert spsc.stats.max_queue_depth <= spsc.stats.queue_capacity
    assert spsc.stats.elapsed_ns > 0
    assert spsc.stats.throughput_events_per_second > 0.0


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
    assert spsc.stats.end_of_stream_markers_produced == 1
    assert spsc.stats.end_of_stream_markers_consumed == 1


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


def test_light_validation_matches_full_checksums() -> None:
    events, symbol_id = _load("sample_replay.csv")
    full = asterion.run_replay(events, symbol_id=symbol_id)

    config = asterion.ReplayConfig()
    config.validation_mode = asterion.ReplayValidationMode.Light
    light = asterion.run_replay(events, symbol_id=symbol_id, config=config)

    assert light.events_processed == full.events_processed
    assert light.final_book_checksum == full.final_book_checksum
    assert light.execution_report_checksum == full.execution_report_checksum
    assert light.diagnostics_checksum == full.diagnostics_checksum
    assert light.sequence_valid == full.sequence_valid
