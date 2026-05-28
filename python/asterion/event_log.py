from __future__ import annotations

import os
from pathlib import Path
from typing import Iterable

from . import _native

PathLike = str | os.PathLike[str]


def _format(value: str | _native.EventLogFormat) -> _native.EventLogFormat:
    if isinstance(value, _native.EventLogFormat):
        return value
    return _native.parse_event_log_format(value)


def detect_format(path: PathLike) -> _native.EventLogFormat:
    return _native.detect_event_log_format(Path(path))


def load_log(
    path: PathLike, format: str | _native.EventLogFormat = _native.EventLogFormat.Auto
) -> list[_native.MarketDataEvent]:
    result = _native.read_event_log(Path(path), _format(format))
    if result.error:
        raise ValueError(result.error)
    return result.events


def write_log(
    path: PathLike,
    events: Iterable[_native.MarketDataEvent],
    format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
) -> _native.EventLogWriteResult:
    result = _native.write_event_log(Path(path), list(events), _format(format))
    if result.error:
        raise ValueError(result.error)
    return result


def convert_log(
    input_path: PathLike,
    output_path: PathLike,
    input_format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
    output_format: str | _native.EventLogFormat = _native.EventLogFormat.Auto,
) -> _native.EventLogWriteResult:
    events = load_log(input_path, input_format)
    return write_log(output_path, events, output_format)
