"""Grouped-vs-shared replay parity coverage through the Python bindings.

Grouped replay (``replay_by_symbol`` / ``aggregate_by_symbol``) is the
correctness-first default. These tests demonstrate, for the tested deterministic
cases, that the opt-in shared multi-symbol path agrees with it, and that the
``describe_replay_parity`` diagnostic reports a clean match. They mirror the C++
parity contract in ``docs/shared_replay_parity.md`` and are not a proof of parity
for all workloads.
"""

from __future__ import annotations

from pathlib import Path

import pytest

# Requires the compiled extension; skipped when it is not built (e.g. offline).
asterion = pytest.importorskip("asterion")

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"


def _event(
    timestamp: int,
    sequence: int,
    symbol: int,
    event_type: "asterion.MarketEventType",
    side: "asterion.Side",
    price: int,
    quantity: int,
    order_id: int,
    flags: int = 0,
) -> "asterion.MarketDataEvent":
    return asterion.MarketDataEvent(
        timestamp_ns=timestamp,
        sequence_number=sequence,
        symbol_id=symbol,
        event_type=event_type,
        side=side,
        price_ticks=price,
        quantity=quantity,
        order_id=order_id,
        trade_id=0,
        flags=flags,
    )


def _strict_config() -> "asterion.AggregateReplayConfig":
    config = asterion.AggregateReplayConfig()
    config.validate_per_symbol_sequences = True
    return config


def _assert_parity(events, config=None) -> None:
    if config is None:
        report = asterion.compare_replay_parity(events)
        description = asterion.describe_replay_parity(events)
    else:
        report = asterion.compare_replay_parity(events, config)
        description = asterion.describe_replay_parity(events, config)

    assert report.matched, description
    assert report.mismatch_count == 0
    assert report.combined_book_checksum_match
    assert report.aggregate_checksum_match
    assert report.symbol_count_grouped == report.symbol_count_shared
    for entry in report.symbols:
        assert entry.present_in_grouped
        assert entry.present_in_shared
        assert entry.matched
    assert description == "replay parity matched"


def test_two_symbol_interleaved_parity() -> None:
    add = asterion.MarketEventType.Add
    cancel = asterion.MarketEventType.Cancel
    replace = asterion.MarketEventType.Replace
    buy = asterion.Side.Buy
    sell = asterion.Side.Sell
    events = [
        _event(1, 1, 1, add, buy, 990, 10, 1),
        _event(2, 2, 2, add, sell, 1010, 8, 2),
        _event(3, 3, 1, add, buy, 985, 5, 3),
        _event(4, 4, 2, replace, sell, 1012, 6, 2),
        _event(5, 5, 1, cancel, buy, 0, 0, 3),
        _event(6, 6, 2, add, buy, 1000, 4, 4),
    ]
    _assert_parity(events)


def test_duplicate_order_id_across_symbols_parity() -> None:
    add = asterion.MarketEventType.Add
    cancel = asterion.MarketEventType.Cancel
    buy = asterion.Side.Buy
    sell = asterion.Side.Sell
    # Order ids are per-symbol; the same id on two symbols is not a duplicate.
    events = [
        _event(1, 1, 1, add, buy, 990, 10, 42),
        _event(2, 2, 2, add, sell, 1010, 8, 42),
        _event(3, 3, 1, cancel, buy, 0, 0, 42),
    ]
    _assert_parity(events)


def test_failure_mode_streams_keep_parity() -> None:
    add = asterion.MarketEventType.Add
    cancel = asterion.MarketEventType.Cancel
    buy = asterion.Side.Buy
    sell = asterion.Side.Sell
    # Symbol 1 has a per-symbol sequence gap; symbol 2 stays contiguous.
    events = [
        _event(1, 1, 1, add, buy, 990, 10, 1),
        _event(2, 1, 2, add, sell, 1010, 8, 2),
        _event(3, 3, 1, cancel, buy, 0, 0, 1),  # gap: expected sequence 2
        _event(4, 2, 2, cancel, sell, 0, 0, 99),  # unknown cancel
    ]
    _assert_parity(events, _strict_config())


def test_parity_is_deterministic_across_runs() -> None:
    add = asterion.MarketEventType.Add
    buy = asterion.Side.Buy
    sell = asterion.Side.Sell
    events = [
        _event(1, 1, 1, add, buy, 990, 10, 1),
        _event(2, 2, 2, add, sell, 1010, 8, 2),
        _event(3, 3, 1, add, buy, 985, 5, 3),
    ]
    first = asterion.compare_replay_parity(events)
    second = asterion.compare_replay_parity(events)
    assert first.grouped_aggregate_checksum == second.grouped_aggregate_checksum
    assert first.shared_aggregate_checksum == second.shared_aggregate_checksum
    assert first.matched and second.matched


def test_sample_file_parity() -> None:
    report = asterion.compare_replay_parity_file(SAMPLES / "sample_replay.csv")
    assert report.matched
    assert report.combined_book_checksum_match
    assert report.aggregate_checksum_match
