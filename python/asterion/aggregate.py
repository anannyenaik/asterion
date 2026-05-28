from __future__ import annotations

import os
from pathlib import Path
from typing import Iterable

from . import _native
from .event_log import _format

PathLike = str | os.PathLike[str]


def aggregate_by_symbol(
    source: PathLike | Iterable[_native.MarketDataEvent],
    format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
    config: _native.AggregateReplayConfig | None = None,
) -> _native.AggregateReplaySummary:
    aggregate_config = config if config is not None else _native.AggregateReplayConfig()
    if isinstance(source, (str, os.PathLike)):
        return _native.replay_file_by_symbol(Path(source), _format(format), aggregate_config)
    return _native.replay_by_symbol(list(source), aggregate_config)
