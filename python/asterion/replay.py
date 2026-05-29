from __future__ import annotations

import os
import random
from pathlib import Path
from typing import Iterable

from . import _native
from .event_log import _format

PathLike = str | os.PathLike[str]


def run_replay(
    source: PathLike | Iterable[_native.MarketDataEvent],
    symbol_id: int = 1,
    format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
    config: _native.ReplayConfig | None = None,
) -> _native.ReplayResult:
    replay_config = config if config is not None else _native.ReplayConfig()
    if isinstance(source, (str, os.PathLike)):
        return _native.replay_file(symbol_id, Path(source), _format(format), replay_config)
    return _native.replay_events(symbol_id, list(source), replay_config)


def collect_diagnostics(
    source: PathLike | Iterable[_native.MarketDataEvent],
    symbol_id: int = 1,
    format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
    config: _native.ReplayConfig | None = None,
) -> list[_native.ReplayDiagnostic]:
    return run_replay(source, symbol_id, format, config).diagnostics


def final_book_checksum_for(
    source: PathLike | Iterable[_native.MarketDataEvent],
    symbol_id: int = 1,
    format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
    config: _native.ReplayConfig | None = None,
) -> int:
    return run_replay(source, symbol_id, format, config).final_book_checksum


def execution_report_checksum_for(
    source: PathLike | Iterable[_native.MarketDataEvent],
    symbol_id: int = 1,
    format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
    config: _native.ReplayConfig | None = None,
) -> int:
    return run_replay(source, symbol_id, format, config).execution_report_checksum


def diagnostics_checksum_for(
    source: PathLike | Iterable[_native.MarketDataEvent],
    symbol_id: int = 1,
    format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
    config: _native.ReplayConfig | None = None,
) -> int:
    return run_replay(source, symbol_id, format, config).diagnostics_checksum


def _fuzz_events(seed: int, event_count: int, symbol_count: int) -> list[_native.MarketDataEvent]:
    rng = random.Random(seed)
    events: list[_native.MarketDataEvent] = []
    active: dict[int, list[int]] = {symbol: [] for symbol in range(1, symbol_count + 1)}
    order_symbol: dict[int, int] = {}
    next_order_id = 1
    sequence = 1
    timestamp = 1

    def append(
        symbol_id: int,
        event_type: _native.MarketEventType,
        side: _native.Side,
        price_ticks: int,
        quantity: int,
        order_id: int,
        flags: int = 0,
    ) -> None:
        nonlocal sequence, timestamp
        events.append(
            _native.MarketDataEvent(
                timestamp_ns=timestamp,
                sequence_number=sequence,
                symbol_id=symbol_id,
                event_type=event_type,
                side=side,
                price_ticks=price_ticks,
                quantity=quantity,
                order_id=order_id,
                trade_id=0,
                flags=flags,
            )
        )
        sequence += 1
        timestamp += 1 + rng.randrange(3)

    while len(events) < event_count:
        symbol = rng.randrange(1, symbol_count + 1)
        roll = rng.randrange(100)
        resting = active[symbol]

        if roll < 12 and len(events) + 3 <= event_count:
            append(symbol, _native.MarketEventType.Snapshot, _native.Side.None_, 0, 0, 0, 1)
            snapshot_orders = list(resting[:2])
            resting[:] = snapshot_orders
            for order_id in snapshot_orders:
                price = 970 + (order_id % 20)
                append(symbol, _native.MarketEventType.Snapshot, _native.Side.Buy, price, 5, order_id)
            append(symbol, _native.MarketEventType.Snapshot, _native.Side.None_, 0, 0, 0, 2)
            continue

        if resting and roll < 38:
            order_id = rng.choice(resting)
            resting.remove(order_id)
            order_symbol.pop(order_id, None)
            append(symbol, _native.MarketEventType.Cancel, _native.Side.Buy, 0, 0, order_id)
            continue

        if resting and roll < 66:
            order_id = rng.choice(resting)
            price = 970 + rng.randrange(20)
            quantity = 1 + rng.randrange(20)
            append(symbol, _native.MarketEventType.Replace, _native.Side.Buy, price, quantity, order_id)
            continue

        order_id = next_order_id
        next_order_id += 1
        resting.append(order_id)
        order_symbol[order_id] = symbol
        append(
            symbol,
            _native.MarketEventType.Add,
            _native.Side.Buy,
            970 + rng.randrange(20),
            1 + rng.randrange(20),
            order_id,
        )

    return events[:event_count]


def shared_replay_fuzz_summary(
    seeds: Iterable[int] = (20260528, 20260529),
    event_count: int = 80,
    symbol_count: int = 4,
) -> dict[str, object]:
    cases: list[dict[str, object]] = []
    mismatch_count = 0
    for seed in seeds:
        events = _fuzz_events(int(seed), event_count, symbol_count)
        report = _native.compare_replay_parity(events, _native.AggregateReplayConfig())
        if not report.matched:
            mismatch_count += 1
        cases.append(
            {
                "seed": int(seed),
                "events": len(events),
                "matched": report.matched,
                "per_symbol_mismatch_count": report.mismatch_count,
                "grouped_aggregate_checksum": report.grouped_aggregate_checksum,
                "shared_aggregate_checksum": report.shared_aggregate_checksum,
                "grouped_combined_book_checksum": report.grouped_combined_book_checksum,
                "shared_combined_book_checksum": report.shared_combined_book_checksum,
                "symbol_count": report.symbol_count_grouped,
            }
        )
    return {
        "case_count": len(cases),
        "mismatch_count": mismatch_count,
        "events_per_case": event_count,
        "symbol_count": symbol_count,
        "cases": cases,
    }
