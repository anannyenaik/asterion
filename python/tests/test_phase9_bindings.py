from __future__ import annotations

from pathlib import Path

import asterion


def _event(
    sequence: int,
    symbol: int,
    event_type: asterion.MarketEventType,
    side: asterion.Side,
    price: int,
    quantity: int,
    order_id: int,
) -> asterion.MarketDataEvent:
    return asterion.MarketDataEvent(
        timestamp_ns=sequence,
        sequence_number=sequence,
        symbol_id=symbol,
        event_type=event_type,
        side=side,
        price_ticks=price,
        quantity=quantity,
        order_id=order_id,
        trade_id=0,
        flags=0,
    )


def _new_order(client_order_id: int, now_ns: int) -> asterion.NewOrderRequest:
    request = asterion.NewOrderRequest()
    request.client_order_id = client_order_id
    request.symbol_id = 1
    request.side = asterion.Side.Buy
    request.order_type = asterion.OrderType.Limit
    request.price_ticks = 1000
    request.quantity = 10
    request.timestamp_ns = now_ns
    request.client_id = 7
    return request


def test_inference_backend_status_reports_optional_onnx_fallback() -> None:
    status = asterion.inference_backend_status(asterion.InferenceBackend.Onnx)
    assert status["requested"] == "onnx"
    assert status["onnx_runtime_available"] is asterion.onnx_runtime_available
    assert status["active"] in {"linear", "onnx"}
    if not asterion.onnx_runtime_available:
        assert status["active"] == "linear"
        assert status["fell_back"] is True


def test_replay_parity_report_is_exposed_to_python() -> None:
    events = [
        _event(1, 1, asterion.MarketEventType.Add, asterion.Side.Buy, 990, 10, 1),
        _event(2, 2, asterion.MarketEventType.Add, asterion.Side.Sell, 1010, 5, 2),
        _event(3, 1, asterion.MarketEventType.Cancel, asterion.Side.Buy, 0, 0, 1),
        _event(4, 2, asterion.MarketEventType.Execute, asterion.Side.Sell, 1010, 2, 2),
    ]

    report = asterion.compare_replay_parity(events)
    assert report.matched
    assert report.mismatch_count == 0
    assert report.symbol_count_grouped == report.symbol_count_shared == 2
    assert all(symbol.matched for symbol in report.symbols)


def test_audit_manifest_signing_round_trips_through_python(tmp_path: Path) -> None:
    log_path = tmp_path / "audit.jsonl"
    risk = asterion.RiskGateway()
    assert risk.open_audit_log(log_path, asterion.RiskAuditLogFormat.Jsonl)
    risk.on_market_data(1, 1000, 0)
    assert risk.check_new_order(_new_order(1, 1), 1).accepted
    risk.close_audit_log()

    key = b"phase9-python-test-key"
    generated = asterion.generate_audit_manifest(
        [log_path],
        signing_key=key,
        signing_key_id="python-test",
    )
    assert generated.ok
    assert generated.manifest.signature is not None
    assert generated.manifest.signature.key_id == "python-test"

    verification = asterion.verify_audit_manifest(generated.manifest, tmp_path, signing_key=key)
    assert verification.valid
    assert verification.signature_present
    assert verification.signature_valid

    wrong_key = asterion.verify_audit_manifest(
        generated.manifest,
        tmp_path,
        signing_key=b"wrong-key",
    )
    assert not wrong_key.valid
    assert any(
        issue.type == asterion.AuditManifestIssueType.SignatureMismatch
        for issue in wrong_key.issues
    )


def test_simulated_broker_session_lifecycle_is_exposed_to_python() -> None:
    session = asterion.SimulatedBrokerSession(True)
    session.connect(1)
    assert session.connected()
    assert session.on_order_accepted(1001, 1, 1, asterion.Side.Buy, 10, 2)

    session.disconnect(3)
    snapshot = session.snapshot()
    assert not session.connected()
    assert snapshot.pending_cancel_count == 1
    assert snapshot.event_count == 4
    assert asterion.session_connection_state_to_string(snapshot.connection_state) == "disconnected"


def test_simulated_portfolio_risk_monitor_is_exposed_to_python() -> None:
    limits = asterion.PortfolioRiskLimits()
    limits.max_gross_exposure_ticks = 50_000
    monitor = asterion.PortfolioRiskMonitor(limits)
    monitor.set_mark(1, 1000)
    monitor.set_audit_enabled(True)

    accepted = monitor.check_order(1, asterion.Side.Buy, 30, 1000, 10)
    rejected = monitor.check_order(1, asterion.Side.Buy, 60, 1000, 11)

    assert accepted.accepted
    assert not rejected.accepted
    assert rejected.breach == asterion.PortfolioBreach.GrossExposure
    assert monitor.audit().size() == 2
    assert (
        monitor.audit().entries()[-1].reject_reason
        == asterion.RejectReason.MaxPortfolioGrossExposure
    )
