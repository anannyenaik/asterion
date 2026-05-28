from __future__ import annotations

import os
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
