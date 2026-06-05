"""An independent, specification-style reference matcher for Asterion.

This module re-implements Asterion's *documented* matching and order-state
semantics (see ``docs/matching_semantics.md``) in plain Python data structures.
It exists as a **second, independent specification** of the same contract so
that Asterion's C++ matching engine can be cross-checked against it on
deterministic golden and random order-flow cases.

Design rules (deliberate):

* It does **not** call Asterion's C++ matching implementation. The only thing
  it borrows from the bindings are the *enum value definitions* (``Side``,
  ``OrderType``, ``TimeInForce``, ``OrderStatus``, ``ExecType``,
  ``RejectReason``) so that its reports are directly comparable to C++
  ``ExecutionReport`` values. None of the matching logic comes from C++.
* It models price-time priority with FIFO queues per price, a bid book and an
  ask book, and an order lookup by exchange id.
* It emits simplified deterministic execution reports with the same fields and
  the same field semantics as Asterion's ``ExecutionReport``.

This is a test oracle. It is not a venue, not a broker, not a production
exchange, and proves nothing about live trading or real-exchange completeness.
"""

from __future__ import annotations

from dataclasses import dataclass, field
from typing import Iterable

from asterion import (
    ExecType,
    OrderStatus,
    OrderType,
    RejectReason,
    Side,
    TimeInForce,
)

# Mirror of the C++ ``core/types.hpp`` sentinels. ``0`` is the invalid/unset id.
INVALID_ORDER_ID = 0
INVALID_CLIENT_ORDER_ID = 0


def _is_valid_side(side: Side) -> bool:
    return side == Side.Buy or side == Side.Sell


def _opposite(side: Side) -> Side:
    if side == Side.Buy:
        return Side.Sell
    if side == Side.Sell:
        return Side.Buy
    return Side.None_


@dataclass
class ReferenceReport:
    """Mirror of Asterion's ``ExecutionReport`` with identical field semantics."""

    client_order_id: int = INVALID_CLIENT_ORDER_ID
    exchange_order_id: int = INVALID_ORDER_ID
    symbol_id: int = 0
    side: Side = Side.None_
    order_status: OrderStatus = OrderStatus.Rejected
    exec_type: ExecType = ExecType.Rejected
    filled_quantity: int = 0
    remaining_quantity: int = 0
    last_fill_quantity: int = 0
    last_fill_price_ticks: int = 0
    average_price_ticks: int = 0
    resting_price_ticks: int = 0
    timestamp_ns: int = 0
    reject_reason: RejectReason = RejectReason.None_


def canonical_report_tuple(report: object) -> tuple:
    """Normalise any report (C++ ``ExecutionReport`` or :class:`ReferenceReport`)
    into a hashable tuple for exact field-by-field comparison.

    Enum-valued fields are compared by ``.name`` so the same value compares
    equal regardless of which binding produced it.
    """

    return (
        int(report.client_order_id),
        int(report.exchange_order_id),
        int(report.symbol_id),
        report.side.name,
        report.order_status.name,
        report.exec_type.name,
        int(report.filled_quantity),
        int(report.remaining_quantity),
        int(report.last_fill_quantity),
        int(report.last_fill_price_ticks),
        int(report.average_price_ticks),
        int(report.resting_price_ticks),
        int(report.timestamp_ns),
        report.reject_reason.name,
    )


@dataclass
class _OrderState:
    client_order_id: int
    exchange_order_id: int
    symbol_id: int
    side: Side
    order_type: OrderType
    limit_price_ticks: int
    original_quantity: int
    client_id: int
    timestamp_ns: int
    filled_quantity: int = 0
    filled_notional_ticks: int = 0
    status: OrderStatus = OrderStatus.New

    @property
    def remaining(self) -> int:
        rem = self.original_quantity - self.filled_quantity
        return rem if rem > 0 else 0

    @property
    def average_price_ticks(self) -> int:
        if self.filled_quantity <= 0:
            return 0
        return self.filled_notional_ticks // self.filled_quantity


class ReferenceMatcher:
    """A small, independent reimplementation of Asterion's matching contract.

    Accepts the same request objects as the C++ bindings
    (``NewOrderRequest`` / ``CancelOrderRequest`` / ``ReplaceOrderRequest``)
    so the same flow can be replayed into both matchers.
    """

    def __init__(self, symbol_id: int) -> None:
        self.symbol_id = symbol_id
        self._next_exchange_order_id = 1
        self._states: dict[int, _OrderState] = {}
        self._client_to_exchange: dict[int, int] = {}
        self._seen_client_order_ids: set[int] = set()
        # FIFO queues per price; ``_in_book`` tracks which orders are resting.
        self._bid_levels: dict[int, list[int]] = {}
        self._ask_levels: dict[int, list[int]] = {}
        self._in_book: set[int] = set()

    # -- canonical snapshots ------------------------------------------------

    def reports_checksum(self) -> int:
        """Stable 64-bit FNV-1a checksum over the canonical session state.

        This is the *reference's own* deterministic checksum (used to assert
        replay stability). It is not required to equal any C++ checksum.
        """

        return self._fnv1a(repr(self.canonical_state()).encode("utf-8"))

    def canonical_state(self) -> dict:
        return {
            "symbol_id": self.symbol_id,
            "l2": self.l2_levels(),
            "open_orders": self.open_orders(),
        }

    def l2_levels(self) -> dict[str, list[tuple[int, int]]]:
        """Aggregate L2 view: ``bids`` high-to-low, ``asks`` low-to-high."""

        bids = [
            (price, self._level_quantity(self._bid_levels[price]))
            for price in sorted(self._bid_levels, reverse=True)
            if self._bid_levels[price]
        ]
        asks = [
            (price, self._level_quantity(self._ask_levels[price]))
            for price in sorted(self._ask_levels)
            if self._ask_levels[price]
        ]
        return {"bids": bids, "asks": asks}

    def open_orders(self) -> list[tuple[int, str, int, int]]:
        """Resting orders as ``(exchange_id, side, price, remaining)`` in a
        deterministic order (side, price, FIFO position)."""

        rows: list[tuple[int, str, int, int]] = []
        for price in sorted(self._bid_levels, reverse=True):
            for oid in self._bid_levels[price]:
                st = self._states[oid]
                rows.append((oid, st.side.name, price, st.remaining))
        for price in sorted(self._ask_levels):
            for oid in self._ask_levels[price]:
                st = self._states[oid]
                rows.append((oid, st.side.name, price, st.remaining))
        return rows

    def book_empty(self) -> bool:
        return not self._in_book

    # -- request entry points ----------------------------------------------

    def submit_order(self, request) -> list[ReferenceReport]:
        reports: list[ReferenceReport] = []
        coid = request.client_order_id
        side = request.side
        ts = request.timestamp_ns

        if request.symbol_id != self.symbol_id or not _is_valid_side(side):
            return [self._reject(coid, request.symbol_id, side, ts, RejectReason.Unsupported)]
        if request.order_type not in (OrderType.Limit, OrderType.Market):
            return [self._reject(coid, request.symbol_id, side, ts, RejectReason.Unsupported)]
        if request.quantity <= 0:
            return [self._reject(coid, request.symbol_id, side, ts, RejectReason.InvalidQuantity)]
        if request.order_type == OrderType.Limit and request.price_ticks <= 0:
            return [self._reject(coid, request.symbol_id, side, ts, RejectReason.InvalidPrice)]
        if request.time_in_force not in (TimeInForce.Gtc, TimeInForce.Ioc, TimeInForce.Fok):
            return [self._reject(coid, request.symbol_id, side, ts, RejectReason.Unsupported)]
        if request.post_only and (
            request.order_type != OrderType.Limit or request.time_in_force != TimeInForce.Gtc
        ):
            return [self._reject(coid, request.symbol_id, side, ts, RejectReason.Unsupported)]
        if not self._reserve_client_order_id(coid):
            return [
                self._reject(coid, request.symbol_id, side, ts, RejectReason.DuplicateClientOrderId)
            ]
        if request.post_only and self._crosses(side, request.order_type, request.price_ticks):
            return [self._reject(coid, request.symbol_id, side, ts, RejectReason.PostOnlyWouldCross)]
        if not request.post_only and self._would_self_trade(
            side, request.order_type, request.price_ticks, request.client_id
        ):
            return [
                self._reject(coid, request.symbol_id, side, ts, RejectReason.SelfTradePrevention)
            ]
        if request.time_in_force == TimeInForce.Fok and not self._can_fully_fill(request):
            return [self._reject(coid, request.symbol_id, side, ts, RejectReason.FokNotFillable)]

        exchange_order_id = self._next_exchange_order_id
        self._next_exchange_order_id += 1
        state = _OrderState(
            client_order_id=coid,
            exchange_order_id=exchange_order_id,
            symbol_id=request.symbol_id,
            side=side,
            order_type=request.order_type,
            limit_price_ticks=request.price_ticks,
            original_quantity=request.quantity,
            client_id=request.client_id,
            timestamp_ns=ts,
        )
        self._states[exchange_order_id] = state
        self._client_to_exchange[coid] = exchange_order_id

        reports.append(self._make_report(state, ExecType.New, 0, 0, ts, RejectReason.None_))

        remaining = self._match_against_book(state, request.quantity, ts, reports)

        if (
            remaining > 0
            and request.order_type == OrderType.Limit
            and request.time_in_force == TimeInForce.Gtc
        ):
            if state.filled_quantity > 0:
                state.status = OrderStatus.PartiallyFilled
            self._rest_limit_order(state)
        elif remaining > 0:
            state.status = OrderStatus.Canceled
            reports.append(
                self._make_report(state, ExecType.Canceled, 0, 0, ts, RejectReason.None_)
            )

        return reports

    def cancel_order(self, request) -> list[ReferenceReport]:
        coid = request.client_order_id
        ts = request.timestamp_ns
        if not self._reserve_client_order_id(coid):
            return [
                self._reject(
                    coid, self.symbol_id, Side.None_, ts, RejectReason.DuplicateClientOrderId
                )
            ]

        state = self._states.get(request.exchange_order_id)
        if state is None or request.exchange_order_id not in self._in_book:
            return [self._reject(coid, self.symbol_id, Side.None_, ts, RejectReason.UnknownOrder)]

        self._remove_from_book(state)
        state.status = OrderStatus.Canceled
        return [self._make_report(state, ExecType.Canceled, 0, 0, ts, RejectReason.None_)]

    def replace_order(self, request) -> list[ReferenceReport]:
        coid = request.client_order_id
        ts = request.timestamp_ns
        if request.new_quantity <= 0:
            return [self._reject(coid, self.symbol_id, Side.None_, ts, RejectReason.InvalidQuantity)]
        if request.new_price_ticks <= 0:
            return [self._reject(coid, self.symbol_id, Side.None_, ts, RejectReason.InvalidPrice)]
        if not self._reserve_client_order_id(coid):
            return [
                self._reject(
                    coid, self.symbol_id, Side.None_, ts, RejectReason.DuplicateClientOrderId
                )
            ]

        state = self._states.get(request.exchange_order_id)
        if state is None or request.exchange_order_id not in self._in_book:
            return [self._reject(coid, self.symbol_id, Side.None_, ts, RejectReason.UnknownOrder)]

        if self._would_self_trade(
            state.side,
            state.order_type,
            request.new_price_ticks,
            state.client_id,
            excluded_order_id=state.exchange_order_id,
        ):
            return [
                self._reject(
                    coid, self.symbol_id, state.side, ts, RejectReason.SelfTradePrevention
                )
            ]

        self._remove_from_book(state)
        state.limit_price_ticks = request.new_price_ticks
        state.original_quantity = state.filled_quantity + request.new_quantity
        state.status = OrderStatus.Replaced
        state.timestamp_ns = ts

        reports = [self._make_report(state, ExecType.Replaced, 0, 0, ts, RejectReason.None_)]

        remaining = self._match_against_book(state, request.new_quantity, ts, reports)
        if remaining > 0:
            if state.filled_quantity > 0 and state.status != OrderStatus.Replaced:
                state.status = OrderStatus.PartiallyFilled
            self._rest_limit_order(state)

        return reports

    # -- matching internals -------------------------------------------------

    def _crosses(self, side: Side, order_type: OrderType, limit_price_ticks: int) -> bool:
        if side == Side.Buy:
            best = self._best_ask()
            if best is None:
                return False
            return order_type == OrderType.Market or limit_price_ticks >= best
        if side == Side.Sell:
            best = self._best_bid()
            if best is None:
                return False
            return order_type == OrderType.Market or limit_price_ticks <= best
        return False

    def _can_fully_fill(self, request) -> bool:
        required = request.quantity
        if request.side == Side.Buy:
            levels = [(p, self._level_quantity(self._ask_levels[p])) for p in sorted(self._ask_levels)]
        else:
            levels = [
                (p, self._level_quantity(self._bid_levels[p]))
                for p in sorted(self._bid_levels, reverse=True)
            ]
        for price, quantity in levels:
            if quantity == 0:
                continue
            executable = (
                request.order_type == OrderType.Market
                or (request.side == Side.Buy and price <= request.price_ticks)
                or (request.side == Side.Sell and price >= request.price_ticks)
            )
            if not executable:
                break
            if quantity >= required:
                return True
            required -= quantity
        return False

    def _would_self_trade(
        self,
        incoming_side: Side,
        order_type: OrderType,
        limit_price_ticks: int,
        client_id: int,
        excluded_order_id: int = INVALID_ORDER_ID,
    ) -> bool:
        if client_id == 0:
            return False
        for order_id in self._in_book:
            if order_id == excluded_order_id:
                continue
            state = self._states[order_id]
            if state.client_id != client_id or state.side != _opposite(incoming_side):
                continue
            if (
                order_type == OrderType.Market
                or (incoming_side == Side.Buy and limit_price_ticks >= state.limit_price_ticks)
                or (incoming_side == Side.Sell and limit_price_ticks <= state.limit_price_ticks)
            ):
                return True
        return False

    def _match_against_book(
        self, incoming: _OrderState, remaining: int, ts: int, reports: list[ReferenceReport]
    ) -> int:
        while remaining > 0 and self._crosses(
            incoming.side, incoming.order_type, incoming.limit_price_ticks
        ):
            resting_oid = self._best_opposing_order(incoming.side)
            if resting_oid is None:
                return remaining
            resting = self._states[resting_oid]
            fill_price = resting.limit_price_ticks
            fill_quantity = min(remaining, resting.remaining)
            remaining -= fill_quantity

            resting.filled_quantity += fill_quantity
            resting.filled_notional_ticks += fill_quantity * fill_price
            resting.status = (
                OrderStatus.Filled if resting.remaining == 0 else OrderStatus.PartiallyFilled
            )

            incoming.filled_quantity += fill_quantity
            incoming.filled_notional_ticks += fill_quantity * fill_price
            incoming.status = (
                OrderStatus.Filled if incoming.remaining == 0 else OrderStatus.PartiallyFilled
            )

            reports.append(
                self._make_report(
                    resting, ExecType.Trade, fill_quantity, fill_price, ts, RejectReason.None_
                )
            )
            reports.append(
                self._make_report(
                    incoming, ExecType.Trade, fill_quantity, fill_price, ts, RejectReason.None_
                )
            )

            if resting.remaining == 0:
                self._remove_from_book(resting)
        return remaining

    def _rest_limit_order(self, state: _OrderState) -> None:
        levels = self._bid_levels if state.side == Side.Buy else self._ask_levels
        levels.setdefault(state.limit_price_ticks, []).append(state.exchange_order_id)
        self._in_book.add(state.exchange_order_id)

    def _remove_from_book(self, state: _OrderState) -> None:
        levels = self._bid_levels if state.side == Side.Buy else self._ask_levels
        queue = levels.get(state.limit_price_ticks)
        if queue and state.exchange_order_id in queue:
            queue.remove(state.exchange_order_id)
            if not queue:
                del levels[state.limit_price_ticks]
        self._in_book.discard(state.exchange_order_id)

    def _best_bid(self):
        prices = [p for p, q in self._bid_levels.items() if q]
        return max(prices) if prices else None

    def _best_ask(self):
        prices = [p for p, q in self._ask_levels.items() if q]
        return min(prices) if prices else None

    def _best_opposing_order(self, incoming_side: Side):
        if incoming_side == Side.Buy:
            best = self._best_ask()
            return self._ask_levels[best][0] if best is not None else None
        best = self._best_bid()
        return self._bid_levels[best][0] if best is not None else None

    def _level_quantity(self, order_ids: Iterable[int]) -> int:
        return sum(self._states[oid].remaining for oid in order_ids)

    def _make_report(
        self,
        state: _OrderState,
        exec_type: ExecType,
        last_fill_quantity: int,
        last_fill_price_ticks: int,
        ts: int,
        reject_reason: RejectReason,
    ) -> ReferenceReport:
        remaining = state.remaining
        if state.status in (OrderStatus.Canceled, OrderStatus.Filled, OrderStatus.Rejected):
            remaining = 0
        resting_price = (
            state.limit_price_ticks
            if remaining > 0 and state.order_type == OrderType.Limit
            else 0
        )
        return ReferenceReport(
            client_order_id=state.client_order_id,
            exchange_order_id=state.exchange_order_id,
            symbol_id=state.symbol_id,
            side=state.side,
            order_status=state.status,
            exec_type=exec_type,
            filled_quantity=state.filled_quantity,
            remaining_quantity=remaining,
            last_fill_quantity=last_fill_quantity,
            last_fill_price_ticks=last_fill_price_ticks,
            average_price_ticks=state.average_price_ticks,
            resting_price_ticks=resting_price,
            timestamp_ns=ts,
            reject_reason=reject_reason,
        )

    def _reject(
        self, client_order_id: int, symbol_id: int, side: Side, ts: int, reason: RejectReason
    ) -> ReferenceReport:
        return ReferenceReport(
            client_order_id=client_order_id,
            exchange_order_id=INVALID_ORDER_ID,
            symbol_id=symbol_id,
            side=side,
            order_status=OrderStatus.Rejected,
            exec_type=ExecType.Rejected,
            timestamp_ns=ts,
            reject_reason=reason,
        )

    def _reserve_client_order_id(self, client_order_id: int) -> bool:
        if client_order_id == INVALID_CLIENT_ORDER_ID:
            return False
        if client_order_id in self._seen_client_order_ids:
            return False
        self._seen_client_order_ids.add(client_order_id)
        return True

    @staticmethod
    def _fnv1a(data: bytes) -> int:
        h = 0xCBF29CE484222325
        for byte in data:
            h ^= byte
            h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
        return h
