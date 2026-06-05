from __future__ import annotations

from pathlib import Path

import pytest

import asterion


ROOT = Path(__file__).resolve().parents[2]
SAMPLES = ROOT / "data" / "samples"


def test_matching_semantics_bindings_are_exposed() -> None:
    request = asterion.NewOrderRequest()
    request.time_in_force = asterion.TimeInForce.Ioc
    request.post_only = True

    assert request.time_in_force == asterion.TimeInForce.Ioc
    assert request.post_only
    assert asterion.RejectReason.PostOnlyWouldCross.name == "PostOnlyWouldCross"
    assert asterion.RejectReason.FokNotFillable.name == "FokNotFillable"


def event(
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


def test_event_log_detection_roundtrip_and_replay_checksums(tmp_path: Path) -> None:
    csv_path = SAMPLES / "sample_replay.csv"
    binary_path = SAMPLES / "sample_replay.bin"

    assert asterion.detect_format(csv_path) == asterion.EventLogFormat.Csv
    assert asterion.detect_format(binary_path) == asterion.EventLogFormat.Binary

    csv_events = asterion.load_log(csv_path)
    binary_events = asterion.load_log(binary_path)
    assert len(csv_events) == len(binary_events) == 5
    assert asterion.checksum_events(csv_events) == asterion.checksum_events(binary_events)

    roundtrip = tmp_path / "roundtrip.bin"
    write_result = asterion.write_log(roundtrip, csv_events, "binary")
    assert write_result.events_written == len(csv_events)
    assert asterion.checksum_events(asterion.load_log(roundtrip)) == write_result.event_checksum

    csv_replay = asterion.run_replay(csv_path, symbol_id=1)
    binary_replay = asterion.run_replay(binary_path, symbol_id=1)
    assert csv_replay.sequence_valid
    assert binary_replay.sequence_valid
    assert csv_replay.final_book_checksum == binary_replay.final_book_checksum
    assert csv_replay.execution_report_checksum == binary_replay.execution_report_checksum
    assert csv_replay.diagnostics_checksum == binary_replay.diagnostics_checksum
    assert asterion.final_book_checksum_for(csv_path, 1) == csv_replay.final_book_checksum
    assert (
        asterion.execution_report_checksum_for(csv_path, 1)
        == csv_replay.execution_report_checksum
    )
    assert asterion.diagnostics_checksum_for(csv_path, 1) == csv_replay.diagnostics_checksum


def test_replay_diagnostics_and_checksum_exposure() -> None:
    events = [
        event(1, 1, asterion.MarketEventType.Add, asterion.Side.Buy, 999, 10, 1),
        event(2, 1, asterion.MarketEventType.Add, asterion.Side.Buy, 998, 10, 1),
    ]

    result = asterion.run_replay(events, symbol_id=1)
    assert not result.sequence_valid
    assert result.diagnostic_error_count == 1
    assert "duplicate order id" in result.diagnostics[0].reason
    assert asterion.checksum_diagnostics(result.diagnostics) == result.diagnostics_checksum


def test_aggregate_multi_symbol_summary() -> None:
    events = [
        event(1, 1, asterion.MarketEventType.Add, asterion.Side.Buy, 999, 10, 1),
        event(2, 2, asterion.MarketEventType.Add, asterion.Side.Sell, 1001, 5, 2),
        event(3, 1, asterion.MarketEventType.Cancel, asterion.Side.Buy, 999, 0, 1),
        asterion.MarketDataEvent(
            timestamp_ns=4,
            sequence_number=4,
            symbol_id=2,
            event_type=asterion.MarketEventType.Execute,
            side=asterion.Side.Sell,
            price_ticks=1001,
            quantity=3,
            order_id=2,
            trade_id=77,
            flags=0,
        ),
    ]

    summary = asterion.aggregate_by_symbol(events)
    assert summary.total_events == 4
    assert summary.symbol_count == 2
    assert [symbol.symbol_id for symbol in summary.symbols] == [1, 2]
    assert [symbol.event_count for symbol in summary.symbols] == [2, 2]
    assert summary.symbols[0].first_sequence == 1
    assert summary.symbols[0].last_sequence == 3
    assert summary.symbols[0].sequence_valid
    assert summary.symbols[1].execution_report_checksum != 0

    strict = asterion.AggregateReplayConfig()
    strict.validate_per_symbol_sequences = True
    strict_summary = asterion.aggregate_by_symbol(events, config=strict)
    assert strict_summary.symbols[0].diagnostic_error_count == 1
    assert "sequence gap" in strict_summary.symbols[0].diagnostics[0].reason


def test_feature_extraction_and_measured_inference() -> None:
    book = asterion.OrderBook(1)
    bid = asterion.Order()
    bid.order_id = 1
    bid.client_order_id = 101
    bid.symbol_id = 1
    bid.side = asterion.Side.Buy
    bid.price_ticks = 999
    bid.quantity = 300
    bid.timestamp_ns = 1
    bid.sequence_number = 1
    ask = asterion.Order()
    ask.order_id = 2
    ask.client_order_id = 102
    ask.symbol_id = 1
    ask.side = asterion.Side.Sell
    ask.price_ticks = 1001
    ask.quantity = 100
    ask.timestamp_ns = 2
    ask.sequence_number = 2

    assert book.add_order(bid)
    assert book.add_order(ask)

    extractor = asterion.FeatureExtractor()
    features = extractor.extract_from_book(book)
    versioned = extractor.extract_versioned_from_book(book)
    assert extractor.feature_version() == 1
    assert versioned.values == features
    assert features == pytest.approx([2.0, 1000.0, 0.5, 400.0])

    model = asterion.LinearModel([0.5, 0.0, 2.0, 0.001], 1.0)
    policy = asterion.InferencePolicy()
    policy.timeout_ns = 1_000_000_000
    result = asterion.measure_linear_inference(model, features, policy)
    assert result.score == pytest.approx(3.4)
    assert result.backend == "linear"
    assert result.accepted
    assert result.inference_latency_ns >= 0

    policy.max_signal_age_ns = 10
    late = asterion.evaluate_inference_policy(policy, 5, 100, 111)
    assert late.late_signal
    assert not late.accepted


def test_benchmark_json_summary_schema_fixture() -> None:
    summary = asterion.summarise_benchmark_json(SAMPLES / "sample_benchmark_schema.json")
    assert summary["schema_version"] == 1
    assert summary["benchmark_count"] == 0
