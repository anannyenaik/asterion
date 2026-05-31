#!/usr/bin/env python3
"""Normalise recorded Binance public depth messages into Asterion replay events.

This is a recorded public market-data engineering tool. It is **not** live
trading, **not** authenticated exchange connectivity, and **not** evidence of
equities-market realism. It reads raw public-depth JSONL captured by
``tools/capture_binance_depth.py`` (or a hand-curated fixture) and produces
Asterion event logs (CSV and/or binary) plus a structured normalisation report.

Determinism: the same input bytes always produce the same normalised events and
the same diagnostics. The parsing/diagnostics core is pure-Python and needs no
network access and no compiled extension; only writing CSV/binary event logs
imports the ``asterion`` package (the project's deterministic event-log writer).

--------------------------------------------------------------------------------
Field mapping (Binance public depth  ->  Asterion event schema)
--------------------------------------------------------------------------------
Binance publishes **L2-style price-level** data: each level is ``[price, qty]``
with no per-order identity. Asterion's schema is **L3/order-oriented** (every
resting order has an ``order_id``). This tool is an honest adapter: it does NOT
pretend Binance exposes real L3 order IDs. It uses *level-replacement* semantics
with *deterministic synthetic* order IDs, and the limitation is documented in
``docs/market_data.md`` and ``LIMITATIONS.md``.

Two raw message shapes are accepted (auto-detected per line):

1. Diff depth update (``@depth`` websocket diff stream), e.g.::

       {"e":"depthUpdate","E":<ms>,"s":"BTCUSDT","U":<firstUpdateId>,
        "u":<finalUpdateId>,"b":[["<price>","<qty>"],...],"a":[[...]]}

   * ``b`` (bids) / ``a`` (asks) -> per-level Asterion events.
   * qty  > 0, level not resting -> ``Add``     (allocate a fresh synthetic id).
   * qty  > 0, level resting     -> ``Replace`` (reuse the level's synthetic id).
   * qty == 0, level resting     -> ``Cancel``  (free the level's synthetic id).
   * qty == 0, level not resting -> no event, ``level_remove_absent`` (info).
   * ``U``/``u`` drive update-continuity diagnostics (gap / stale).
   * ``E`` (ms) -> ``timestamp_ns = E * 1_000_000``; reversal -> diagnostic.

2. Book snapshot (REST ``/api/v3/depth`` or partial-depth stream), e.g.::

       {"lastUpdateId":<id>,"bids":[["<price>","<qty>"],...],"asks":[[...]]}

   A snapshot is emitted as an Asterion **snapshot block** (begin marker, one
   ``Snapshot`` record per resting level, end marker) which the replay engine
   uses to reconstruct the book. ``lastUpdateId`` drives continuity diagnostics
   across consecutive snapshots.

Capture envelope: ``tools/capture_binance_depth.py`` wraps each fetched REST
response so the (otherwise absent) symbol and capture time are preserved::

       {"_captured_at_ns":<ns>,"symbol":"BTCUSDT","endpoint":"...","data":<resp>}

Lines whose JSON object carries a ``_meta`` or ``_fixture`` key are treated as
metadata banners (recorded, not normalised).

Per-field mapping:

    Asterion field     <- Binance source
    -----------------     ----------------------------------------------------
    timestamp_ns       <- E (ms) * 1e6 | envelope _captured_at_ns | synthetic+1
    sequence_number    <- synthetic monotonic counter (1..N) over emitted events
    symbol_id          <- deterministic small id per symbol (first-seen order)
    event_type         <- Add / Replace / Cancel / Snapshot (see rules above)
    side               <- Buy for bids, Sell for asks
    price_ticks        <- round(Decimal(price) * PRICE_SCALE)   (integer ticks)
    quantity           <- round(Decimal(qty)   * QTY_SCALE)     (integer units)
    order_id           <- deterministic synthetic id per resting price level
    trade_id           <- 0 (depth data carries no trades)
    flags              <- snapshot begin/end markers only
"""

from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass, field
from decimal import Decimal, InvalidOperation
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Tuple

TOOL_VERSION = "1.0.0"

# Integer scaling: Binance public spot data carries up to 8 decimal places.
# Scaling by 1e8 is lossless for that precision and keeps values comfortably
# inside a signed 64-bit integer for the demo symbols (BTCUSDT / ETHUSDT).
PRICE_SCALE = 100_000_000
QTY_SCALE = 100_000_000

# Asterion event-type / side integer codes (mirror cpp/include/asterion/market_data/event.hpp
# and risk enums): Add=1 Cancel=2 Replace=3 Execute=4 Trade=5 Snapshot=6 Heartbeat=7;
# Side None=0 Buy=1 Sell=2.
ET_ADD = 1
ET_CANCEL = 2
ET_REPLACE = 3
ET_SNAPSHOT = 6
SIDE_NONE = 0
SIDE_BUY = 1
SIDE_SELL = 2
_ET_NAME = {ET_ADD: "Add", ET_CANCEL: "Cancel", ET_REPLACE: "Replace", ET_SNAPSHOT: "Snapshot"}

# Snapshot framing flags (mirror asterion::kSnapshotBeginFlag / kSnapshotEndFlag).
SNAPSHOT_BEGIN_FLAG = 0x1
SNAPSHOT_END_FLAG = 0x2

# Stable severity / reason codes for a deterministic diagnostics checksum.
# (Python's hash() is per-process randomised, so it is never used for checksums.)
SEVERITY_INFO = "info"
SEVERITY_WARNING = "warning"
SEVERITY_ERROR = "error"
_SEVERITY_CODE = {SEVERITY_INFO: 1, SEVERITY_WARNING: 2, SEVERITY_ERROR: 3}

REASON_MALFORMED_MESSAGE = "malformed_message"
REASON_MISSING_FIELDS = "missing_fields"
REASON_UNSUPPORTED_MESSAGE_TYPE = "unsupported_message_type"
REASON_NON_MONOTONIC_EVENT_TIME = "non_monotonic_event_time"
REASON_UPDATE_GAP = "update_gap"
REASON_STALE_UPDATE = "stale_update"
REASON_CROSSED_BOOK = "crossed_book"
REASON_INVALID_PRICE = "invalid_price"
REASON_INVALID_QUANTITY = "invalid_quantity"
REASON_LEVEL_REMOVE_ABSENT = "level_remove_absent"
_REASON_CODE = {
    REASON_MALFORMED_MESSAGE: 1,
    REASON_MISSING_FIELDS: 2,
    REASON_UNSUPPORTED_MESSAGE_TYPE: 3,
    REASON_NON_MONOTONIC_EVENT_TIME: 4,
    REASON_UPDATE_GAP: 5,
    REASON_STALE_UPDATE: 6,
    REASON_CROSSED_BOOK: 7,
    REASON_INVALID_PRICE: 8,
    REASON_INVALID_QUANTITY: 9,
    REASON_LEVEL_REMOVE_ABSENT: 10,
}

# FNV-1a checksum recipe (matches scripts/asterion_inspect.py for consistency).
_FNV_OFFSET = 14695981039346656037
_FNV_PRIME = 1099511628211
_MASK64 = (1 << 64) - 1


def _fnv_byte(seed: int, byte: int) -> int:
    seed ^= byte & 0xFF
    return (seed * _FNV_PRIME) & _MASK64


def _fnv_int(seed: int, value: int, byte_count: int) -> int:
    value &= (1 << (byte_count * 8)) - 1
    for index in range(byte_count):
        seed = _fnv_byte(seed, (value >> (index * 8)) & 0xFF)
    return seed


@dataclass
class NormEvent:
    """A normalised Asterion event in pure-Python form (no extension needed)."""

    timestamp_ns: int
    sequence_number: int
    symbol_id: int
    event_type: int
    side: int
    price_ticks: int
    quantity: int
    order_id: int
    trade_id: int = 0
    flags: int = 0


@dataclass
class NormalisationDiagnostic:
    raw_index: int  # 0-based index of the source line that triggered it
    severity: str
    reason: str
    detail: str = ""


@dataclass
class NormalisationResult:
    events: List[NormEvent] = field(default_factory=list)
    diagnostics: List[NormalisationDiagnostic] = field(default_factory=list)
    metadata: List[dict] = field(default_factory=list)
    symbol_ids: Dict[str, int] = field(default_factory=dict)
    raw_message_count: int = 0
    raw_line_count: int = 0

    @property
    def event_type_counts(self) -> Dict[str, int]:
        counts: Dict[str, int] = {}
        for ev in self.events:
            key = _ET_NAME.get(ev.event_type, str(ev.event_type)).lower()
            counts[key] = counts.get(key, 0) + 1
        return counts

    @property
    def diagnostics_by_reason(self) -> Dict[str, int]:
        counts: Dict[str, int] = {}
        for d in self.diagnostics:
            counts[d.reason] = counts.get(d.reason, 0) + 1
        return counts

    @property
    def diagnostics_by_severity(self) -> Dict[str, int]:
        counts: Dict[str, int] = {}
        for d in self.diagnostics:
            counts[d.severity] = counts.get(d.severity, 0) + 1
        return counts

    @property
    def events_checksum(self) -> int:
        acc = _FNV_OFFSET
        for ev in self.events:
            acc = _fnv_int(acc, ev.timestamp_ns, 8)
            acc = _fnv_int(acc, ev.sequence_number, 8)
            acc = _fnv_int(acc, ev.symbol_id, 4)
            acc = _fnv_int(acc, ev.event_type, 1)
            acc = _fnv_int(acc, ev.side, 1)
            acc = _fnv_int(acc, ev.price_ticks, 8)
            acc = _fnv_int(acc, ev.quantity, 8)
            acc = _fnv_int(acc, ev.order_id, 8)
            acc = _fnv_int(acc, ev.trade_id, 8)
            acc = _fnv_int(acc, ev.flags, 4)
        return acc

    @property
    def diagnostics_checksum(self) -> int:
        acc = _FNV_OFFSET
        for d in self.diagnostics:
            acc = _fnv_int(acc, d.raw_index, 8)
            acc = _fnv_int(acc, _SEVERITY_CODE.get(d.severity, 0), 1)
            acc = _fnv_int(acc, _REASON_CODE.get(d.reason, 0), 1)
        return acc

    def report(self, source: str = "") -> dict:
        ts = [e.timestamp_ns for e in self.events]
        return {
            "tool_version": TOOL_VERSION,
            "source": source,
            "symbols": sorted(self.symbol_ids),
            "symbol_ids": dict(sorted(self.symbol_ids.items())),
            "raw_line_count": self.raw_line_count,
            "raw_message_count": self.raw_message_count,
            "metadata_line_count": len(self.metadata),
            "normalised_event_count": len(self.events),
            "event_type_counts": self.event_type_counts,
            "diagnostics_count": len(self.diagnostics),
            "diagnostics_by_reason": self.diagnostics_by_reason,
            "diagnostics_by_severity": self.diagnostics_by_severity,
            "first_timestamp_ns": ts[0] if ts else 0,
            "last_timestamp_ns": ts[-1] if ts else 0,
            "first_sequence": self.events[0].sequence_number if self.events else 0,
            "last_sequence": self.events[-1].sequence_number if self.events else 0,
            "events_checksum": self.events_checksum,
            "diagnostics_checksum": self.diagnostics_checksum,
        }


class _Normaliser:
    """Stateful, deterministic single-pass normaliser."""

    def __init__(self) -> None:
        self.result = NormalisationResult()
        self._seq = 0
        self._next_order_id = 1
        self._active: Dict[Tuple[int, int, int], int] = {}  # (sym, side, price) -> order_id
        self._levels: Dict[Tuple[int, int, int], int] = {}  # (sym, side, price) -> qty
        self._last_update_id: Dict[int, int] = {}
        self._last_event_time: Dict[int, int] = {}
        self._next_symbol_id = 1

    def _symbol_id(self, symbol: str) -> int:
        sid = self.result.symbol_ids.get(symbol)
        if sid is None:
            sid = self._next_symbol_id
            self.result.symbol_ids[symbol] = sid
            self._next_symbol_id += 1
        return sid

    def _diag(self, raw_index: int, severity: str, reason: str, detail: str = "") -> None:
        self.result.diagnostics.append(NormalisationDiagnostic(raw_index, severity, reason, detail))

    def _emit(self, **kwargs) -> None:
        self._seq += 1
        self.result.events.append(NormEvent(sequence_number=self._seq, **kwargs))

    @staticmethod
    def _to_ticks(value: object, scale: int) -> Optional[int]:
        try:
            return int((Decimal(str(value)) * scale).to_integral_value())
        except (InvalidOperation, ValueError, TypeError):
            return None

    def _best_bid_ask(self, symbol_id: int) -> Tuple[Optional[int], Optional[int]]:
        bids = [p for (s, side, p), q in self._levels.items() if s == symbol_id and side == SIDE_BUY and q > 0]
        asks = [p for (s, side, p), q in self._levels.items() if s == symbol_id and side == SIDE_SELL and q > 0]
        return (max(bids) if bids else None, min(asks) if asks else None)

    def _check_crossed(self, raw_index: int, symbol_id: int) -> None:
        best_bid, best_ask = self._best_bid_ask(symbol_id)
        if best_bid is not None and best_ask is not None and best_bid >= best_ask:
            self._diag(
                raw_index,
                SEVERITY_WARNING,
                REASON_CROSSED_BOOK,
                f"best_bid={best_bid} >= best_ask={best_ask}",
            )

    def _apply_level(
        self, raw_index: int, ts_ns: int, symbol_id: int, side: int, price_str: object, qty_str: object
    ) -> None:
        price_ticks = self._to_ticks(price_str, PRICE_SCALE)
        qty = self._to_ticks(qty_str, QTY_SCALE)
        if price_ticks is None or price_ticks <= 0:
            self._diag(raw_index, SEVERITY_ERROR, REASON_INVALID_PRICE, f"price={price_str!r}")
            return
        if qty is None or qty < 0:
            self._diag(raw_index, SEVERITY_ERROR, REASON_INVALID_QUANTITY, f"qty={qty_str!r}")
            return

        key = (symbol_id, side, price_ticks)
        if qty == 0:
            order_id = self._active.pop(key, None)
            self._levels.pop(key, None)
            if order_id is None:
                self._diag(
                    raw_index,
                    SEVERITY_INFO,
                    REASON_LEVEL_REMOVE_ABSENT,
                    f"side={side} price_ticks={price_ticks}",
                )
                return
            self._emit(
                timestamp_ns=ts_ns, symbol_id=symbol_id, event_type=ET_CANCEL, side=side,
                price_ticks=price_ticks, quantity=0, order_id=order_id,
            )
            return

        order_id = self._active.get(key)
        if order_id is None:
            order_id = self._next_order_id
            self._next_order_id += 1
            self._active[key] = order_id
            self._levels[key] = qty
            self._emit(
                timestamp_ns=ts_ns, symbol_id=symbol_id, event_type=ET_ADD, side=side,
                price_ticks=price_ticks, quantity=qty, order_id=order_id,
            )
        else:
            self._levels[key] = qty
            self._emit(
                timestamp_ns=ts_ns, symbol_id=symbol_id, event_type=ET_REPLACE, side=side,
                price_ticks=price_ticks, quantity=qty, order_id=order_id,
            )

    def _handle_diff(self, raw_index: int, msg: dict, symbol: str, ts_ns: int) -> None:
        symbol_id = self._symbol_id(symbol)

        last_ts = self._last_event_time.get(symbol_id)
        if last_ts is not None and ts_ns < last_ts:
            self._diag(raw_index, SEVERITY_WARNING, REASON_NON_MONOTONIC_EVENT_TIME, f"{ts_ns} < {last_ts}")
        self._last_event_time[symbol_id] = ts_ns

        first_id = msg.get("U")
        final_id = msg.get("u")
        last_u = self._last_update_id.get(symbol_id)
        if isinstance(first_id, int) and isinstance(final_id, int):
            if last_u is not None:
                if final_id <= last_u:
                    self._diag(raw_index, SEVERITY_WARNING, REASON_STALE_UPDATE, f"u={final_id} <= last_u={last_u}")
                elif first_id > last_u + 1:
                    self._diag(raw_index, SEVERITY_WARNING, REASON_UPDATE_GAP, f"U={first_id} expected {last_u + 1}")
            self._last_update_id[symbol_id] = final_id

        for price, qty in _iter_levels(msg.get("b", []), descending=True):
            self._apply_level(raw_index, ts_ns, symbol_id, SIDE_BUY, price, qty)
        for price, qty in _iter_levels(msg.get("a", []), descending=False):
            self._apply_level(raw_index, ts_ns, symbol_id, SIDE_SELL, price, qty)

        self._check_crossed(raw_index, symbol_id)

    def _handle_snapshot(self, raw_index: int, msg: dict, symbol: str, ts_ns: int) -> None:
        symbol_id = self._symbol_id(symbol)

        last_id = msg.get("lastUpdateId")
        last_u = self._last_update_id.get(symbol_id)
        if isinstance(last_id, int):
            if last_u is not None and last_id < last_u:
                self._diag(raw_index, SEVERITY_WARNING, REASON_STALE_UPDATE, f"lastUpdateId={last_id} < {last_u}")
            self._last_update_id[symbol_id] = max(last_id, last_u if last_u is not None else last_id)

        levels: List[Tuple[int, int, int]] = []  # (side, price_ticks, qty)
        for source_side, key in ((SIDE_BUY, "bids"), (SIDE_SELL, "asks")):
            for price, qty in _iter_levels(msg.get(key, []), descending=(source_side == SIDE_BUY)):
                pt = self._to_ticks(price, PRICE_SCALE)
                q = self._to_ticks(qty, QTY_SCALE)
                if pt is None or pt <= 0:
                    self._diag(raw_index, SEVERITY_ERROR, REASON_INVALID_PRICE, f"price={price!r}")
                    continue
                if q is None or q <= 0:
                    continue
                levels.append((source_side, pt, q))

        # The snapshot is authoritative: drop this symbol's prior resting state.
        for key in [k for k in self._active if k[0] == symbol_id]:
            self._active.pop(key, None)
            self._levels.pop(key, None)

        self._emit(
            timestamp_ns=ts_ns, symbol_id=symbol_id, event_type=ET_SNAPSHOT, side=SIDE_NONE,
            price_ticks=0, quantity=0, order_id=0, flags=SNAPSHOT_BEGIN_FLAG,
        )
        for side, pt, q in levels:
            order_id = self._next_order_id
            self._next_order_id += 1
            self._active[(symbol_id, side, pt)] = order_id
            self._levels[(symbol_id, side, pt)] = q
            self._emit(
                timestamp_ns=ts_ns, symbol_id=symbol_id, event_type=ET_SNAPSHOT, side=side,
                price_ticks=pt, quantity=q, order_id=order_id,
            )
        self._emit(
            timestamp_ns=ts_ns, symbol_id=symbol_id, event_type=ET_SNAPSHOT, side=SIDE_NONE,
            price_ticks=0, quantity=0, order_id=0, flags=SNAPSHOT_END_FLAG,
        )

        self._check_crossed(raw_index, symbol_id)

    def feed_line(self, raw_index: int, line: str) -> None:
        stripped = line.strip()
        if not stripped:
            return
        self.result.raw_line_count += 1
        try:
            obj = json.loads(stripped)
        except json.JSONDecodeError as exc:
            self._diag(raw_index, SEVERITY_ERROR, REASON_MALFORMED_MESSAGE, str(exc))
            return
        if not isinstance(obj, dict):
            self._diag(raw_index, SEVERITY_ERROR, REASON_MALFORMED_MESSAGE, "not a JSON object")
            return

        if "_meta" in obj or "_fixture" in obj:
            self.result.metadata.append(obj)
            self.result.raw_line_count -= 1  # banners are not raw messages
            return

        symbol_hint: Optional[str] = None
        ts_hint: Optional[int] = None
        if "data" in obj and isinstance(obj["data"], dict):
            symbol_hint = obj.get("symbol")
            if isinstance(obj.get("_captured_at_ns"), int):
                ts_hint = obj["_captured_at_ns"]
            obj = obj["data"]

        self.result.raw_message_count += 1

        is_diff = ("b" in obj or "a" in obj) and ("U" in obj or "u" in obj or obj.get("e") == "depthUpdate")
        is_snapshot = "lastUpdateId" in obj or "bids" in obj or "asks" in obj

        if is_diff:
            symbol = obj.get("s") or symbol_hint
            if not symbol:
                self._diag(raw_index, SEVERITY_ERROR, REASON_MISSING_FIELDS, "no symbol")
                return
            event_time = obj.get("E")
            if isinstance(event_time, int):
                ts_ns = event_time * 1_000_000
            elif ts_hint is not None:
                ts_ns = ts_hint
            else:
                ts_ns = self._last_event_time.get(self._symbol_id(symbol), 0) + 1
            self._handle_diff(raw_index, obj, symbol, ts_ns)
        elif is_snapshot:
            symbol = obj.get("s") or symbol_hint
            if not symbol:
                self._diag(raw_index, SEVERITY_ERROR, REASON_MISSING_FIELDS, "no symbol for snapshot")
                return
            ts_ns = ts_hint if ts_hint is not None else self._last_event_time.get(self._symbol_id(symbol), 0) + 1
            self._last_event_time[self._symbol_id(symbol)] = ts_ns
            self._handle_snapshot(raw_index, obj, symbol, ts_ns)
        else:
            self._diag(raw_index, SEVERITY_WARNING, REASON_UNSUPPORTED_MESSAGE_TYPE, f"e={obj.get('e', 'unknown')!r}")


def _iter_levels(levels: object, descending: bool) -> List[Tuple[str, str]]:
    """Return ``[(price, qty), ...]`` in a deterministic, canonical order.

    Binance arrays arrive sorted, but we re-sort defensively so output is
    independent of source ordering: bids best-first (descending), asks best-first
    (ascending). Entries that are not 2-element pairs are dropped.
    """
    out: List[Tuple[str, str]] = []
    if not isinstance(levels, list):
        return out
    for entry in levels:
        if isinstance(entry, (list, tuple)) and len(entry) >= 2:
            out.append((str(entry[0]), str(entry[1])))

    def _key(item: Tuple[str, str]) -> Decimal:
        try:
            return Decimal(item[0])
        except (InvalidOperation, ValueError):
            return Decimal(0)

    out.sort(key=_key, reverse=descending)
    return out


def normalise_lines(lines: Iterable[str]) -> NormalisationResult:
    """Normalise an iterable of raw JSONL lines deterministically."""
    norm = _Normaliser()
    for index, line in enumerate(lines):
        norm.feed_line(index, line)
    return norm.result


def normalise_file(path: str | Path) -> NormalisationResult:
    return normalise_lines(Path(path).read_text(encoding="utf-8").splitlines())


def to_market_events(events: Iterable[NormEvent]) -> list:
    """Convert NormEvents to ``asterion.MarketDataEvent`` (imports the extension)."""
    import asterion  # noqa: PLC0415 - lazy so the pure core stays import-light.

    et = {
        ET_ADD: asterion.MarketEventType.Add,
        ET_CANCEL: asterion.MarketEventType.Cancel,
        ET_REPLACE: asterion.MarketEventType.Replace,
        ET_SNAPSHOT: asterion.MarketEventType.Snapshot,
    }
    side = {SIDE_NONE: asterion.Side.None_, SIDE_BUY: asterion.Side.Buy, SIDE_SELL: asterion.Side.Sell}
    return [
        asterion.MarketDataEvent(
            timestamp_ns=e.timestamp_ns,
            sequence_number=e.sequence_number,
            symbol_id=e.symbol_id,
            event_type=et[e.event_type],
            side=side[e.side],
            price_ticks=e.price_ticks,
            quantity=e.quantity,
            order_id=e.order_id,
            trade_id=e.trade_id,
            flags=e.flags,
        )
        for e in events
    ]


def write_event_logs(
    events: Iterable[NormEvent], csv_output: Optional[str] = None, binary_output: Optional[str] = None
) -> None:
    """Write CSV and/or binary Asterion event logs via the deterministic writer."""
    import asterion  # noqa: PLC0415

    market_events = to_market_events(events)
    if csv_output:
        Path(csv_output).parent.mkdir(parents=True, exist_ok=True)
        asterion.write_log(Path(csv_output), market_events, "csv")
    if binary_output:
        Path(binary_output).parent.mkdir(parents=True, exist_ok=True)
        asterion.write_log(Path(binary_output), market_events, "binary")


def main(argv: Optional[List[str]] = None) -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    parser.add_argument("--input", required=True, help="raw Binance depth JSONL path")
    parser.add_argument("--csv-output", help="write normalised events as CSV")
    parser.add_argument("--binary-output", help="write normalised events as binary")
    parser.add_argument("--report-output", help="write the JSON normalisation report")
    parser.add_argument("--json", action="store_true", help="print the report as JSON to stdout")
    parser.add_argument("--quiet", action="store_true", help="suppress the text summary")
    args = parser.parse_args(argv)

    result = normalise_file(args.input)
    report = result.report(source=str(args.input))

    if args.csv_output or args.binary_output:
        write_event_logs(result.events, args.csv_output, args.binary_output)
    if args.report_output:
        Path(args.report_output).parent.mkdir(parents=True, exist_ok=True)
        Path(args.report_output).write_text(
            json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8"
        )

    if args.json:
        print(json.dumps(report, indent=2, sort_keys=True))
    elif not args.quiet:
        print(f"input: {args.input}")
        print(f"symbols: {report['symbols']}")
        print(f"raw_messages: {report['raw_message_count']}")
        print(f"normalised_events: {report['normalised_event_count']}")
        print(f"event_type_counts: {report['event_type_counts']}")
        print(f"diagnostics_count: {report['diagnostics_count']}")
        print(f"diagnostics_by_reason: {report['diagnostics_by_reason']}")
        print(f"events_checksum: {report['events_checksum']}")
        print(f"diagnostics_checksum: {report['diagnostics_checksum']}")
        if args.csv_output:
            print(f"csv_output: {args.csv_output}")
        if args.binary_output:
            print(f"binary_output: {args.binary_output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
