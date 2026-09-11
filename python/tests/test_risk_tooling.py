from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

import pytest

asterion = pytest.importorskip("asterion")

ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"
CLI = ROOT / "scripts" / "asterion_inspect.py"


def test_python_risk_exposure_snapshot_and_sliding_mode() -> None:
    limits = asterion.RiskLimits()
    limits.max_open_order_quantity = 150
    limits.max_messages_per_window = 2
    limits.rate_window_ns = 100
    limits.rate_limit_mode = asterion.RateLimitMode.SlidingWindow

    risk = asterion.RiskGateway(limits)
    risk.on_market_data(1, 1000, 0)

    first = asterion.NewOrderRequest()
    first.client_order_id = 1
    first.client_id = 7
    first.symbol_id = 1
    first.side = asterion.Side.Buy
    first.order_type = asterion.OrderType.Limit
    first.price_ticks = 1000
    first.quantity = 100
    first.timestamp_ns = 10

    assert risk.check_new_order(first, 10).accepted
    snapshot = risk.exposure_snapshot()
    assert snapshot.working_quantity[1] == 100
    assert asterion.rate_limit_mode_to_string(snapshot.rate_limit_mode) == "sliding-window"

    fill = asterion.ExecutionReport()
    fill.client_order_id = 1
    fill.symbol_id = 1
    fill.side = asterion.Side.Buy
    fill.order_status = asterion.OrderStatus.Filled
    fill.exec_type = asterion.ExecType.Trade
    fill.filled_quantity = 100
    fill.remaining_quantity = 0
    fill.timestamp_ns = 20
    risk.on_execution_report(fill)

    assert dict(risk.exposure_snapshot().working_quantity) == {}


def test_python_replace_risk_and_disconnect_summary() -> None:
    limits = asterion.RiskLimits()
    limits.max_open_order_quantity = 150
    limits.max_order_quantity = 200
    limits.cancel_on_disconnect = True

    risk = asterion.RiskGateway(limits)
    risk.set_audit_enabled(True)
    risk.on_market_data(1, 1000, 0)

    order = asterion.NewOrderRequest()
    order.client_order_id = 1
    order.client_id = 7
    order.symbol_id = 1
    order.side = asterion.Side.Buy
    order.order_type = asterion.OrderType.Limit
    order.price_ticks = 1000
    order.quantity = 100
    order.timestamp_ns = 10
    assert risk.check_new_order(order, 10).accepted

    report = asterion.ExecutionReport()
    report.client_order_id = 1
    report.exchange_order_id = 10001
    report.symbol_id = 1
    report.side = asterion.Side.Buy
    report.order_status = asterion.OrderStatus.New
    report.exec_type = asterion.ExecType.New
    report.remaining_quantity = 100
    report.resting_price_ticks = 1000
    risk.on_execution_report(report)

    replace = asterion.ReplaceOrderRequest()
    replace.client_order_id = 20
    replace.exchange_order_id = 10001
    replace.new_price_ticks = 1001
    replace.new_quantity = 120
    replace.timestamp_ns = 20
    assert risk.check_replace_order(replace, 20).accepted
    assert risk.working_quantity(1) == 120

    risk.on_disconnect(30)
    snapshot = risk.exposure_snapshot()
    assert snapshot.connected is False
    assert snapshot.disconnect_count == 1
    assert snapshot.disconnect_cancel_count == 1
    assert dict(snapshot.working_quantity) == {}


def test_risk_exposure_cli_json() -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(CLI),
            "risk-exposure",
            "--input",
            str(SAMPLES / "sample_risk_flow.json"),
            "--json",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["rate_limit_mode"] == "sliding-window"
    assert payload["working_quantity"] == {"1": 40}


def test_risk_exposure_cli_reports_replaces_and_disconnects() -> None:
    result = subprocess.run(
        [
            sys.executable,
            str(CLI),
            "risk-exposure",
            "--input",
            str(SAMPLES / "sample_disconnect_replace_risk.json"),
            "--json",
        ],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    payload = json.loads(result.stdout)
    assert payload["connected"] is False
    assert payload["disconnect_count"] == 1
    assert payload["disconnect_cancel_count"] == 1
    assert payload["working_quantity"] == {}
    replace_decisions = [item for item in payload["decisions"] if item["type"] == "replace"]
    assert replace_decisions[0]["accepted"] is True
    assert replace_decisions[1]["reject_reason"] == "MaxOpenOrderQuantity"
