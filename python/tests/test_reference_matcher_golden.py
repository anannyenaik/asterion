"""Deterministic golden cross-checks: hand-written order flows replayed into
both Asterion's C++ matching engine and the independent Python reference
matcher. Every case asserts the two agree *and* pins the expected outcome, so
the test is also a readable specification of the documented semantics.
"""

from __future__ import annotations

import asterion
from asterion.testing.cross_check import (
    cancel_order,
    new_order,
    replace_order,
    run_cross_check,
)

Side = asterion.Side
OrderType = asterion.OrderType
TimeInForce = asterion.TimeInForce
OrderStatus = asterion.OrderStatus
ExecType = asterion.ExecType
RejectReason = asterion.RejectReason

LIMIT = OrderType.Limit
MARKET = OrderType.Market


def _check(ops):
    result = run_cross_check(ops)
    assert result.matched, "C++ and reference matcher disagree:\n" + result.detail
    return result


def test_simple_gtc_rest():
    result = _check(
        [new_order(1, Side.Buy, LIMIT, 1000, 10, 1)]
    )
    reports = result.cpp_reports
    assert len(reports) == 1
    assert reports[0].exec_type == ExecType.New
    assert reports[0].order_status == OrderStatus.New
    assert result.cpp_l2["bids"] == [(1000, 10)]


def test_market_order_full_fill():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 10, 1),
            new_order(2, Side.Buy, MARKET, 0, 10, 2),
        ]
    )
    assert result.cpp_reports[-1].order_status == OrderStatus.Filled
    assert result.cpp_reports[-1].filled_quantity == 10
    assert result.cpp_l2 == {"bids": [], "asks": []}


def test_partial_fill_then_rest():
    # Incoming GTC buy larger than resting ask: fills part, rests remainder.
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 4, 1),
            new_order(2, Side.Buy, LIMIT, 1001, 10, 2),
        ]
    )
    last = result.cpp_reports[-1]
    assert last.exec_type == ExecType.Trade
    assert last.order_status == OrderStatus.PartiallyFilled
    assert last.filled_quantity == 4
    assert result.cpp_l2["bids"] == [(1001, 6)]
    assert result.cpp_l2["asks"] == []


def test_ioc_full_fill():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 10, 1),
            new_order(2, Side.Buy, LIMIT, 1001, 10, 2, time_in_force=TimeInForce.Ioc),
        ]
    )
    assert result.cpp_reports[-1].order_status == OrderStatus.Filled
    assert result.cpp_l2 == {"bids": [], "asks": []}


def test_ioc_partial_fill_cancels_remainder():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 4, 1),
            new_order(2, Side.Buy, LIMIT, 1001, 10, 2, time_in_force=TimeInForce.Ioc),
        ]
    )
    last = result.cpp_reports[-1]
    assert last.exec_type == ExecType.Canceled
    assert last.order_status == OrderStatus.Canceled
    assert last.filled_quantity == 4
    assert last.remaining_quantity == 0
    assert result.cpp_l2 == {"bids": [], "asks": []}


def test_ioc_no_fill_no_rest():
    result = _check(
        [new_order(1, Side.Buy, LIMIT, 1000, 7, 1, time_in_force=TimeInForce.Ioc)]
    )
    reports = result.cpp_reports
    assert len(reports) == 2
    assert reports[0].exec_type == ExecType.New
    assert reports[-1].exec_type == ExecType.Canceled
    assert reports[-1].filled_quantity == 0
    assert result.cpp_l2 == {"bids": [], "asks": []}


def test_fok_full_fill_price_time_priority():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 4, 1),
            new_order(2, Side.Sell, LIMIT, 1001, 6, 2),
            new_order(3, Side.Buy, LIMIT, 1001, 10, 3, time_in_force=TimeInForce.Fok),
        ]
    )
    reports = result.cpp_reports
    # New(rest1) + New(rest2) + [New + 2 paired trade reports] for the FOK.
    assert len(reports) == 7
    trade_reports = [r for r in reports if r.exec_type == ExecType.Trade]
    assert len(trade_reports) == 4
    assert reports[-1].order_status == OrderStatus.Filled
    assert result.cpp_l2 == {"bids": [], "asks": []}


def test_fok_insufficient_leaves_book_unchanged():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 5, 1),
            new_order(2, Side.Buy, LIMIT, 1001, 6, 2, time_in_force=TimeInForce.Fok),
        ]
    )
    reports = result.cpp_reports
    assert reports[-1].order_status == OrderStatus.Rejected
    assert reports[-1].reject_reason == RejectReason.FokNotFillable
    assert result.cpp_l2["asks"] == [(1001, 5)]


def test_post_only_non_crossing_rests():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1002, 10, 1),
            new_order(2, Side.Buy, LIMIT, 1001, 5, 2, post_only=True),
        ]
    )
    assert result.cpp_reports[-1].exec_type == ExecType.New
    assert result.cpp_l2["bids"] == [(1001, 5)]
    assert result.cpp_l2["asks"] == [(1002, 10)]


def test_post_only_crossing_rejects_without_trade():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 10, 1),
            new_order(2, Side.Buy, LIMIT, 1001, 5, 2, post_only=True),
        ]
    )
    reports = result.cpp_reports
    assert reports[-1].reject_reason == RejectReason.PostOnlyWouldCross
    assert reports[-1].exec_type == ExecType.Rejected
    assert result.cpp_l2["asks"] == [(1001, 10)]


def test_cancel_existing_order():
    # Order 1 gets exchange id 1; cancel it by exchange id.
    result = _check(
        [
            new_order(1, Side.Buy, LIMIT, 1000, 10, 1),
            cancel_order(2, 1, 2),
        ]
    )
    assert result.cpp_reports[-1].exec_type == ExecType.Canceled
    assert result.cpp_reports[-1].order_status == OrderStatus.Canceled
    assert result.cpp_l2 == {"bids": [], "asks": []}


def test_cancel_unknown_order_rejects():
    result = _check(
        [cancel_order(1, 999, 1)]
    )
    assert result.cpp_reports[-1].exec_type == ExecType.Rejected
    assert result.cpp_reports[-1].reject_reason == RejectReason.UnknownOrder


def test_replace_loses_priority():
    # Two resting bids at the same price; replacing the first re-inserts it at
    # the back, so an incoming sell hits the *other* (now-front) order first.
    result = _check(
        [
            new_order(1, Side.Buy, LIMIT, 1000, 5, 1),
            new_order(2, Side.Buy, LIMIT, 1000, 5, 2),
            replace_order(3, 1, 1000, 5, 3),
            new_order(4, Side.Sell, LIMIT, 1000, 5, 4),
        ]
    )
    # The sell trades against exchange id 2 (the order that kept priority).
    trade_reports = [r for r in result.cpp_reports if r.exec_type == ExecType.Trade]
    resting_trade = trade_reports[0]
    assert resting_trade.exchange_order_id == 2
    assert result.cpp_l2["bids"] == [(1000, 5)]


def test_replace_self_cross_rejects():
    result = _check(
        [
            new_order(1, Side.Buy, LIMIT, 999, 10, 1, client_id=7),
            new_order(2, Side.Sell, LIMIT, 1002, 10, 2, client_id=7),
            replace_order(3, 1, 1002, 10, 3),
        ]
    )
    assert result.cpp_reports[-1].reject_reason == RejectReason.SelfTradePrevention
    # Original buy keeps its place; both orders still resting.
    assert result.cpp_l2["bids"] == [(999, 10)]
    assert result.cpp_l2["asks"] == [(1002, 10)]


def test_stp_same_client_crossing_rejects():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 10, 1, client_id=7),
            new_order(2, Side.Buy, LIMIT, 1001, 10, 2, client_id=7),
        ]
    )
    assert result.cpp_reports[-1].reject_reason == RejectReason.SelfTradePrevention
    assert result.cpp_l2["asks"] == [(1001, 10)]


def test_different_client_crossing_matches():
    result = _check(
        [
            new_order(1, Side.Sell, LIMIT, 1001, 10, 1, client_id=7),
            new_order(2, Side.Buy, LIMIT, 1001, 10, 2, client_id=8),
        ]
    )
    assert result.cpp_reports[-1].order_status == OrderStatus.Filled
    assert result.cpp_l2 == {"bids": [], "asks": []}


def test_duplicate_client_order_id_rejects():
    result = _check(
        [
            new_order(1, Side.Buy, LIMIT, 1000, 10, 1),
            new_order(1, Side.Buy, LIMIT, 1000, 10, 2),
        ]
    )
    assert result.cpp_reports[-1].reject_reason == RejectReason.DuplicateClientOrderId
    assert result.cpp_l2["bids"] == [(1000, 10)]


def test_invalid_quantity_rejects():
    result = _check(
        [new_order(1, Side.Buy, LIMIT, 1000, 0, 1)]
    )
    assert result.cpp_reports[-1].reject_reason == RejectReason.InvalidQuantity
