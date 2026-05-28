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
