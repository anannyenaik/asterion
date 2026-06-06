#include "asterion/book/order.hpp"
#include "asterion/book/order_book.hpp"
#include "asterion/inference/backend.hpp"
#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/inference/torchscript_model.hpp"
#include "asterion/market_data/event.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/market_data/replay_aggregate.hpp"
#include "asterion/market_data/spsc_replay.hpp"
#include "asterion/matching/execution_report.hpp"
#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/audit_manifest.hpp"
#include "asterion/risk/portfolio_risk.hpp"
#include "asterion/risk/risk_gateway.hpp"
#include "asterion/session/broker_session.hpp"

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl/filesystem.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace py = pybind11;

namespace {

using namespace asterion;

EventLogFormat parse_format_or_throw(std::string_view value) {
  const auto format = parse_event_log_format(value);
  if (!format) {
    throw std::invalid_argument("unknown event-log format: " + std::string(value));
  }
  return *format;
}

ReplayResult replay_file_for_symbol(SymbolId symbol_id, const std::filesystem::path& path,
                                    EventLogFormat format, ReplayConfig config) {
  ReplayEngine replay(symbol_id, config);
  return replay.replay_file(path, format);
}

ReplayResult replay_events_for_symbol(SymbolId symbol_id, const std::vector<MarketDataEvent>& events,
                                      ReplayConfig config) {
  ReplayEngine replay(symbol_id, config);
  return replay.replay_events(events);
}

} // namespace

PYBIND11_MODULE(_native, module) {
  module.doc() = "Thin C++ bindings for Asterion replay, checksums and measured inference.";

  py::enum_<asterion::Side>(module, "Side")
      .value("None_", asterion::Side::None)
      .value("Buy", asterion::Side::Buy)
      .value("Sell", asterion::Side::Sell);

  py::enum_<asterion::MarketEventType>(module, "MarketEventType")
      .value("Add", asterion::MarketEventType::Add)
      .value("Cancel", asterion::MarketEventType::Cancel)
      .value("Replace", asterion::MarketEventType::Replace)
      .value("Execute", asterion::MarketEventType::Execute)
      .value("Trade", asterion::MarketEventType::Trade)
      .value("Snapshot", asterion::MarketEventType::Snapshot)
      .value("Heartbeat", asterion::MarketEventType::Heartbeat);

  py::enum_<asterion::EventLogFormat>(module, "EventLogFormat")
      .value("Auto", asterion::EventLogFormat::Auto)
      .value("Csv", asterion::EventLogFormat::Csv)
      .value("Binary", asterion::EventLogFormat::Binary);

  py::enum_<asterion::ReplayDiagnosticSeverity>(module, "ReplayDiagnosticSeverity")
      .value("Info", asterion::ReplayDiagnosticSeverity::Info)
      .value("Warning", asterion::ReplayDiagnosticSeverity::Warning)
      .value("Error", asterion::ReplayDiagnosticSeverity::Error);

  py::enum_<asterion::OrderStatus>(module, "OrderStatus")
      .value("New", asterion::OrderStatus::New)
      .value("PartiallyFilled", asterion::OrderStatus::PartiallyFilled)
      .value("Filled", asterion::OrderStatus::Filled)
      .value("Canceled", asterion::OrderStatus::Canceled)
      .value("Replaced", asterion::OrderStatus::Replaced)
      .value("Rejected", asterion::OrderStatus::Rejected);

  py::enum_<asterion::OrderType>(module, "OrderType")
      .value("Limit", asterion::OrderType::Limit)
      .value("Market", asterion::OrderType::Market);

  py::enum_<asterion::TimeInForce>(module, "TimeInForce")
      .value("Gtc", asterion::TimeInForce::Gtc)
      .value("Ioc", asterion::TimeInForce::Ioc)
      .value("Fok", asterion::TimeInForce::Fok);

  py::enum_<asterion::ExecType>(module, "ExecType")
      .value("New", asterion::ExecType::New)
      .value("Trade", asterion::ExecType::Trade)
      .value("Canceled", asterion::ExecType::Canceled)
      .value("Replaced", asterion::ExecType::Replaced)
      .value("Rejected", asterion::ExecType::Rejected);

  py::enum_<asterion::RejectReason>(module, "RejectReason")
      .value("None_", asterion::RejectReason::None)
      .value("InvalidQuantity", asterion::RejectReason::InvalidQuantity)
      .value("InvalidPrice", asterion::RejectReason::InvalidPrice)
      .value("DuplicateClientOrderId", asterion::RejectReason::DuplicateClientOrderId)
      .value("UnknownOrder", asterion::RejectReason::UnknownOrder)
      .value("KillSwitch", asterion::RejectReason::KillSwitch)
      .value("MaxOrderQuantity", asterion::RejectReason::MaxOrderQuantity)
      .value("MaxNotional", asterion::RejectReason::MaxNotional)
      .value("MaxPosition", asterion::RejectReason::MaxPosition)
      .value("MaxGrossExposure", asterion::RejectReason::MaxGrossExposure)
      .value("PriceBand", asterion::RejectReason::PriceBand)
      .value("StaleMarketData", asterion::RejectReason::StaleMarketData)
      .value("Unsupported", asterion::RejectReason::Unsupported)
      .value("InternalError", asterion::RejectReason::InternalError)
      .value("MaxOpenOrderQuantity", asterion::RejectReason::MaxOpenOrderQuantity)
      .value("MessageRateLimit", asterion::RejectReason::MessageRateLimit)
      .value("SelfTradePrevention", asterion::RejectReason::SelfTradePrevention)
      .value("Disconnected", asterion::RejectReason::Disconnected)
      .value("MaxPortfolioGrossExposure", asterion::RejectReason::MaxPortfolioGrossExposure)
      .value("MaxPortfolioNetExposure", asterion::RejectReason::MaxPortfolioNetExposure)
      .value("MaxSymbolConcentration", asterion::RejectReason::MaxSymbolConcentration)
      .value("MaxPortfolioLoss", asterion::RejectReason::MaxPortfolioLoss)
      .value("PostOnlyWouldCross", asterion::RejectReason::PostOnlyWouldCross)
      .value("FokNotFillable", asterion::RejectReason::FokNotFillable);

  py::enum_<asterion::RateLimitMode>(module, "RateLimitMode")
      .value("FixedWindow", asterion::RateLimitMode::FixedWindow)
      .value("SlidingWindow", asterion::RateLimitMode::SlidingWindow);

  py::enum_<asterion::DisconnectOrderPolicy>(module, "DisconnectOrderPolicy")
      .value("RejectNewOrders", asterion::DisconnectOrderPolicy::RejectNewOrders)
      .value("AllowNewOrders", asterion::DisconnectOrderPolicy::AllowNewOrders);

  py::enum_<asterion::RiskAuditLogFormat>(module, "RiskAuditLogFormat")
      .value("Text", asterion::RiskAuditLogFormat::Text)
      .value("Jsonl", asterion::RiskAuditLogFormat::Jsonl);

  py::enum_<asterion::InferenceDecision>(module, "InferenceDecision")
      .value("Accept", asterion::InferenceDecision::Accept)
      .value("Timeout", asterion::InferenceDecision::Timeout)
      .value("LateSignal", asterion::InferenceDecision::LateSignal)
      .value("TimeoutAndLateSignal", asterion::InferenceDecision::TimeoutAndLateSignal);

  py::class_<asterion::MarketDataEvent>(module, "MarketDataEvent")
      .def(py::init([](asterion::TimestampNs timestamp_ns,
                       asterion::SequenceNumber sequence_number, asterion::SymbolId symbol_id,
                       asterion::MarketEventType event_type, asterion::Side side,
                       asterion::PriceTicks price_ticks, asterion::Quantity quantity,
                       asterion::OrderId order_id, asterion::TradeId trade_id,
                       std::uint32_t flags) {
             return asterion::MarketDataEvent{timestamp_ns, sequence_number, symbol_id,
                                              event_type, side, price_ticks, quantity,
                                              order_id, trade_id, flags};
           }),
           py::arg("timestamp_ns") = 0, py::arg("sequence_number") = 0,
           py::arg("symbol_id") = asterion::kInvalidSymbolId,
           py::arg("event_type") = asterion::MarketEventType::Heartbeat,
           py::arg("side") = asterion::Side::None, py::arg("price_ticks") = 0,
           py::arg("quantity") = 0, py::arg("order_id") = asterion::kInvalidOrderId,
           py::arg("trade_id") = 0, py::arg("flags") = 0)
      .def_readwrite("timestamp_ns", &asterion::MarketDataEvent::timestamp_ns)
      .def_readwrite("sequence_number", &asterion::MarketDataEvent::sequence_number)
      .def_readwrite("symbol_id", &asterion::MarketDataEvent::symbol_id)
      .def_readwrite("event_type", &asterion::MarketDataEvent::event_type)
      .def_readwrite("side", &asterion::MarketDataEvent::side)
      .def_readwrite("price_ticks", &asterion::MarketDataEvent::price_ticks)
      .def_readwrite("quantity", &asterion::MarketDataEvent::quantity)
      .def_readwrite("order_id", &asterion::MarketDataEvent::order_id)
      .def_readwrite("trade_id", &asterion::MarketDataEvent::trade_id)
      .def_readwrite("flags", &asterion::MarketDataEvent::flags);

  py::class_<asterion::EventLogReadResult>(module, "EventLogReadResult")
      .def_readwrite("events", &asterion::EventLogReadResult::events)
      .def_readwrite("detected_format", &asterion::EventLogReadResult::detected_format)
      .def_readwrite("event_checksum", &asterion::EventLogReadResult::event_checksum)
      .def_readwrite("error", &asterion::EventLogReadResult::error);

  py::class_<asterion::EventLogWriteResult>(module, "EventLogWriteResult")
      .def_readwrite("events_written", &asterion::EventLogWriteResult::events_written)
      .def_readwrite("event_checksum", &asterion::EventLogWriteResult::event_checksum)
      .def_readwrite("error", &asterion::EventLogWriteResult::error);

  py::class_<asterion::ReplayDiagnostic>(module, "ReplayDiagnostic")
      .def_readwrite("event_index", &asterion::ReplayDiagnostic::event_index)
      .def_readwrite("sequence_number", &asterion::ReplayDiagnostic::sequence_number)
      .def_readwrite("symbol_id", &asterion::ReplayDiagnostic::symbol_id)
      .def_readwrite("severity", &asterion::ReplayDiagnostic::severity)
      .def_readwrite("reason", &asterion::ReplayDiagnostic::reason);

  py::enum_<asterion::ReplayValidationMode>(module, "ReplayValidationMode")
      .value("Full", asterion::ReplayValidationMode::Full)
      .value("Light", asterion::ReplayValidationMode::Light);

  py::class_<asterion::ReplayConfig>(module, "ReplayConfig")
      .def(py::init<>())
      .def_readwrite("validate_sequence_numbers",
                     &asterion::ReplayConfig::validate_sequence_numbers)
      .def_readwrite("validate_timestamps", &asterion::ReplayConfig::validate_timestamps)
      .def_readwrite("validate_book_state", &asterion::ReplayConfig::validate_book_state)
      .def_readwrite("validation_mode", &asterion::ReplayConfig::validation_mode)
      .def_readwrite("max_speed", &asterion::ReplayConfig::max_speed);

  py::class_<asterion::ReplayResult>(module, "ReplayResult")
      .def_readwrite("events_processed", &asterion::ReplayResult::events_processed)
      .def_readwrite("sequence_valid", &asterion::ReplayResult::sequence_valid)
      .def_readwrite("event_log_checksum", &asterion::ReplayResult::event_log_checksum)
      .def_readwrite("final_book_checksum", &asterion::ReplayResult::final_book_checksum)
      .def_readwrite("execution_report_checksum",
                     &asterion::ReplayResult::execution_report_checksum)
      .def_readwrite("diagnostics_checksum", &asterion::ReplayResult::diagnostics_checksum)
      .def_readwrite("diagnostic_error_count",
                     &asterion::ReplayResult::diagnostic_error_count)
      .def_readwrite("diagnostic_warning_count",
                     &asterion::ReplayResult::diagnostic_warning_count)
      .def_readwrite("diagnostics", &asterion::ReplayResult::diagnostics)
      .def_readwrite("error", &asterion::ReplayResult::error);

  // Opt-in SPSC replay pipeline. Deterministic single-thread replay remains the
  // default; this exposes the bounded single-producer/single-consumer pipeline
  // for reviewer-facing parity and stats inspection.
  py::enum_<asterion::SpscBackpressurePolicy>(module, "SpscBackpressurePolicy")
      .value("Block", asterion::SpscBackpressurePolicy::Block)
      .value("DropNewestOnFull", asterion::SpscBackpressurePolicy::DropNewestOnFull);

  py::class_<asterion::SpscReplayConfig>(module, "SpscReplayConfig")
      .def(py::init<>())
      .def_readwrite("queue_capacity", &asterion::SpscReplayConfig::queue_capacity)
      .def_readwrite("backpressure", &asterion::SpscReplayConfig::backpressure)
      .def_readwrite("replay", &asterion::SpscReplayConfig::replay);

  py::class_<asterion::SpscReplayStats>(module, "SpscReplayStats")
      .def_readonly("produced_events", &asterion::SpscReplayStats::produced_events)
      .def_readonly("consumed_events", &asterion::SpscReplayStats::consumed_events)
      .def_readonly("queue_capacity", &asterion::SpscReplayStats::queue_capacity)
      .def_readonly("max_queue_depth", &asterion::SpscReplayStats::max_queue_depth)
      .def_readonly("backpressure_count", &asterion::SpscReplayStats::backpressure_count)
      .def_readonly("dropped_events", &asterion::SpscReplayStats::dropped_events)
      .def_readonly("end_of_stream_seen", &asterion::SpscReplayStats::end_of_stream_seen)
      .def_readonly("end_of_stream_markers_produced",
                    &asterion::SpscReplayStats::end_of_stream_markers_produced)
      .def_readonly("end_of_stream_markers_consumed",
                    &asterion::SpscReplayStats::end_of_stream_markers_consumed)
      .def_readonly("drop_policy_enabled", &asterion::SpscReplayStats::drop_policy_enabled)
      .def_readonly("elapsed_ns", &asterion::SpscReplayStats::elapsed_ns)
      .def_readonly("throughput_events_per_second",
                    &asterion::SpscReplayStats::throughput_events_per_second);

  py::class_<asterion::SpscReplayResult>(module, "SpscReplayResult")
      .def_readonly("replay", &asterion::SpscReplayResult::replay)
      .def_readonly("stats", &asterion::SpscReplayResult::stats);

  py::class_<asterion::AggregateReplayConfig>(module, "AggregateReplayConfig")
      .def(py::init<>())
      .def_readwrite("replay_config", &asterion::AggregateReplayConfig::replay_config)
      .def_readwrite("validate_per_symbol_sequences",
                     &asterion::AggregateReplayConfig::validate_per_symbol_sequences);

  py::class_<asterion::SymbolReplaySummary>(module, "SymbolReplaySummary")
      .def_readwrite("symbol_id", &asterion::SymbolReplaySummary::symbol_id)
      .def_readwrite("event_count", &asterion::SymbolReplaySummary::event_count)
      .def_readwrite("first_sequence", &asterion::SymbolReplaySummary::first_sequence)
      .def_readwrite("last_sequence", &asterion::SymbolReplaySummary::last_sequence)
      .def_readwrite("first_timestamp_ns", &asterion::SymbolReplaySummary::first_timestamp_ns)
      .def_readwrite("last_timestamp_ns", &asterion::SymbolReplaySummary::last_timestamp_ns)
      .def_readwrite("sequence_valid", &asterion::SymbolReplaySummary::sequence_valid)
      .def_readwrite("event_log_checksum", &asterion::SymbolReplaySummary::event_log_checksum)
      .def_readwrite("final_book_checksum",
                     &asterion::SymbolReplaySummary::final_book_checksum)
      .def_readwrite("execution_report_checksum",
                     &asterion::SymbolReplaySummary::execution_report_checksum)
      .def_readwrite("diagnostics_checksum",
                     &asterion::SymbolReplaySummary::diagnostics_checksum)
      .def_readwrite("diagnostic_count", &asterion::SymbolReplaySummary::diagnostic_count)
      .def_readwrite("diagnostic_error_count",
                     &asterion::SymbolReplaySummary::diagnostic_error_count)
      .def_readwrite("diagnostic_warning_count",
                     &asterion::SymbolReplaySummary::diagnostic_warning_count)
      .def_readwrite("diagnostics", &asterion::SymbolReplaySummary::diagnostics)
      .def_readwrite("error", &asterion::SymbolReplaySummary::error);

  py::class_<asterion::AggregateReplaySummary>(module, "AggregateReplaySummary")
      .def_readwrite("total_events", &asterion::AggregateReplaySummary::total_events)
      .def_readwrite("symbol_count", &asterion::AggregateReplaySummary::symbol_count)
      .def_readwrite("combined_book_checksum",
                     &asterion::AggregateReplaySummary::combined_book_checksum)
      .def_readwrite("aggregate_checksum", &asterion::AggregateReplaySummary::aggregate_checksum)
      .def_readwrite("symbols", &asterion::AggregateReplaySummary::symbols)
      .def_readwrite("error", &asterion::AggregateReplaySummary::error);

  py::class_<asterion::L2Level>(module, "L2Level")
      .def(py::init<>())
      .def_readwrite("price_ticks", &asterion::L2Level::price_ticks)
      .def_readwrite("quantity", &asterion::L2Level::quantity);

  py::class_<asterion::L2View>(module, "L2View")
      .def(py::init<>())
      .def_readwrite("symbol_id", &asterion::L2View::symbol_id)
      .def_readwrite("bids", &asterion::L2View::bids)
      .def_readwrite("asks", &asterion::L2View::asks);

  py::class_<asterion::Order>(module, "Order")
      .def(py::init<>())
      .def_readwrite("order_id", &asterion::Order::order_id)
      .def_readwrite("client_order_id", &asterion::Order::client_order_id)
      .def_readwrite("symbol_id", &asterion::Order::symbol_id)
      .def_readwrite("side", &asterion::Order::side)
      .def_readwrite("price_ticks", &asterion::Order::price_ticks)
      .def_readwrite("quantity", &asterion::Order::quantity)
      .def_readwrite("timestamp_ns", &asterion::Order::timestamp_ns)
      .def_readwrite("sequence_number", &asterion::Order::sequence_number);

  py::class_<asterion::OrderBook>(module, "OrderBook")
      .def(py::init<asterion::SymbolId>())
      .def("symbol_id", &asterion::OrderBook::symbol_id)
      .def("order_count", &asterion::OrderBook::order_count)
      .def("empty", &asterion::OrderBook::empty)
      .def("add_order", &asterion::OrderBook::add_order)
      .def("best_bid", &asterion::OrderBook::best_bid)
      .def("best_ask", &asterion::OrderBook::best_ask)
      .def("l2_view", &asterion::OrderBook::l2_view)
      .def("checksum", &asterion::OrderBook::checksum);

  py::class_<asterion::FeatureVector>(module, "FeatureVector")
      .def_readwrite("version", &asterion::FeatureVector::version)
      .def_readwrite("names", &asterion::FeatureVector::names)
      .def_readwrite("values", &asterion::FeatureVector::values);

  py::class_<asterion::FeatureExtractor>(module, "FeatureExtractor")
      .def(py::init<>())
      .def("feature_version", &asterion::FeatureExtractor::feature_version)
      .def("feature_names", &asterion::FeatureExtractor::feature_names)
      .def("extract", &asterion::FeatureExtractor::extract)
      .def("extract_versioned", &asterion::FeatureExtractor::extract_versioned)
      .def("extract_from_book", &asterion::FeatureExtractor::extract_from_book,
           py::arg("book"), py::arg("depth") = 1)
      .def("extract_versioned_from_book",
           &asterion::FeatureExtractor::extract_versioned_from_book, py::arg("book"),
           py::arg("depth") = 1);

  py::class_<asterion::LinearModel>(module, "LinearModel")
      .def(py::init<std::vector<double>, double>())
      .def("backend_name", [](const asterion::LinearModel& model) {
        return std::string(model.backend_name());
      })
      .def("score", [](const asterion::LinearModel& model, const std::vector<double>& features) {
        return model.score(features);
      });

  py::class_<asterion::TorchScriptModel>(module, "TorchScriptModel")
      .def(py::init<std::filesystem::path>())
      .def("backend_name", [](const asterion::TorchScriptModel& model) {
        return std::string(model.backend_name());
      })
      .def("available", &asterion::TorchScriptModel::available)
      .def("model_path", &asterion::TorchScriptModel::model_path)
      .def("load_error", &asterion::TorchScriptModel::load_error)
      .def("score", [](const asterion::TorchScriptModel& model,
                       const std::vector<double>& features) { return model.score(features); });

  py::class_<asterion::InferencePolicy>(module, "InferencePolicy")
      .def(py::init<>())
      .def_readwrite("timeout_ns", &asterion::InferencePolicy::timeout_ns)
      .def_readwrite("max_signal_age_ns", &asterion::InferencePolicy::max_signal_age_ns)
      .def_readwrite("drop_timed_out", &asterion::InferencePolicy::drop_timed_out)
      .def_readwrite("drop_late_signals", &asterion::InferencePolicy::drop_late_signals);

  py::class_<asterion::InferencePolicyResult>(module, "InferencePolicyResult")
      .def_readwrite("timed_out", &asterion::InferencePolicyResult::timed_out)
      .def_readwrite("late_signal", &asterion::InferencePolicyResult::late_signal)
      .def_readwrite("accepted", &asterion::InferencePolicyResult::accepted)
      .def_readwrite("decision", &asterion::InferencePolicyResult::decision);

  py::class_<asterion::InferenceResult>(module, "InferenceResult")
      .def_readwrite("score", &asterion::InferenceResult::score)
      .def_readwrite("inference_latency_ns",
                     &asterion::InferenceResult::inference_latency_ns)
      .def_readwrite("timed_out", &asterion::InferenceResult::timed_out)
      .def_readwrite("late_signal", &asterion::InferenceResult::late_signal)
      .def_readwrite("accepted", &asterion::InferenceResult::accepted)
      .def_readwrite("decision", &asterion::InferenceResult::decision)
      .def_readwrite("backend", &asterion::InferenceResult::backend)
      .def_readwrite("model_name", &asterion::InferenceResult::model_name)
      .def_readwrite("input_shape", &asterion::InferenceResult::input_shape)
      .def_readwrite("output_shape", &asterion::InferenceResult::output_shape);

  py::class_<asterion::ExecutionReport>(module, "ExecutionReport")
      .def(py::init<>())
      .def_readwrite("client_order_id", &asterion::ExecutionReport::client_order_id)
      .def_readwrite("exchange_order_id", &asterion::ExecutionReport::exchange_order_id)
      .def_readwrite("symbol_id", &asterion::ExecutionReport::symbol_id)
      .def_readwrite("side", &asterion::ExecutionReport::side)
      .def_readwrite("order_status", &asterion::ExecutionReport::order_status)
      .def_readwrite("exec_type", &asterion::ExecutionReport::exec_type)
      .def_readwrite("filled_quantity", &asterion::ExecutionReport::filled_quantity)
      .def_readwrite("remaining_quantity", &asterion::ExecutionReport::remaining_quantity)
      .def_readwrite("last_fill_quantity", &asterion::ExecutionReport::last_fill_quantity)
      .def_readwrite("last_fill_price_ticks",
                     &asterion::ExecutionReport::last_fill_price_ticks)
      .def_readwrite("average_price_ticks", &asterion::ExecutionReport::average_price_ticks)
      .def_readwrite("resting_price_ticks", &asterion::ExecutionReport::resting_price_ticks)
      .def_readwrite("timestamp_ns", &asterion::ExecutionReport::timestamp_ns)
      .def_readwrite("reject_reason", &asterion::ExecutionReport::reject_reason);

  py::class_<asterion::NewOrderRequest>(module, "NewOrderRequest")
      .def(py::init<>())
      .def_readwrite("client_order_id", &asterion::NewOrderRequest::client_order_id)
      .def_readwrite("symbol_id", &asterion::NewOrderRequest::symbol_id)
      .def_readwrite("side", &asterion::NewOrderRequest::side)
      .def_readwrite("order_type", &asterion::NewOrderRequest::order_type)
      .def_readwrite("price_ticks", &asterion::NewOrderRequest::price_ticks)
      .def_readwrite("quantity", &asterion::NewOrderRequest::quantity)
      .def_readwrite("timestamp_ns", &asterion::NewOrderRequest::timestamp_ns)
      .def_readwrite("client_id", &asterion::NewOrderRequest::client_id)
      .def_readwrite("time_in_force", &asterion::NewOrderRequest::time_in_force)
      .def_readwrite("post_only", &asterion::NewOrderRequest::post_only);

  py::class_<asterion::ReplaceOrderRequest>(module, "ReplaceOrderRequest")
      .def(py::init<>())
      .def_readwrite("client_order_id", &asterion::ReplaceOrderRequest::client_order_id)
      .def_readwrite("exchange_order_id", &asterion::ReplaceOrderRequest::exchange_order_id)
      .def_readwrite("new_price_ticks", &asterion::ReplaceOrderRequest::new_price_ticks)
      .def_readwrite("new_quantity", &asterion::ReplaceOrderRequest::new_quantity)
      .def_readwrite("timestamp_ns", &asterion::ReplaceOrderRequest::timestamp_ns);

  py::class_<asterion::CancelOrderRequest>(module, "CancelOrderRequest")
      .def(py::init<>())
      .def_readwrite("client_order_id", &asterion::CancelOrderRequest::client_order_id)
      .def_readwrite("exchange_order_id", &asterion::CancelOrderRequest::exchange_order_id)
      .def_readwrite("timestamp_ns", &asterion::CancelOrderRequest::timestamp_ns);

  // Deterministic in-process matching engine. Exposed read-only for
  // specification-style reference testing against an independent Python model;
  // this is not a live or production exchange surface.
  py::class_<asterion::MatchingEngine>(module, "MatchingEngine")
      .def(py::init<asterion::SymbolId>(), py::arg("symbol_id"))
      .def("submit_order", &asterion::MatchingEngine::submit_order, py::arg("request"))
      .def("cancel_order", &asterion::MatchingEngine::cancel_order, py::arg("request"))
      .def("replace_order", &asterion::MatchingEngine::replace_order, py::arg("request"))
      .def("book", &asterion::MatchingEngine::book,
           py::return_value_policy::reference_internal)
      .def("reports_checksum", &asterion::MatchingEngine::reports_checksum);

  py::class_<asterion::RiskLimits>(module, "RiskLimits")
      .def(py::init<>())
      .def_readwrite("max_order_quantity", &asterion::RiskLimits::max_order_quantity)
      .def_readwrite("max_notional_ticks", &asterion::RiskLimits::max_notional_ticks)
      .def_readwrite("max_position_per_symbol",
                     &asterion::RiskLimits::max_position_per_symbol)
      .def_readwrite("max_gross_exposure_ticks",
                     &asterion::RiskLimits::max_gross_exposure_ticks)
      .def_readwrite("price_band_ticks", &asterion::RiskLimits::price_band_ticks)
      .def_readwrite("stale_after_ns", &asterion::RiskLimits::stale_after_ns)
      .def_readwrite("max_open_order_quantity",
                     &asterion::RiskLimits::max_open_order_quantity)
      .def_readwrite("max_messages_per_window",
                     &asterion::RiskLimits::max_messages_per_window)
      .def_readwrite("rate_window_ns", &asterion::RiskLimits::rate_window_ns)
      .def_readwrite("rate_limit_mode", &asterion::RiskLimits::rate_limit_mode)
      .def_readwrite("enable_self_trade_prevention",
                     &asterion::RiskLimits::enable_self_trade_prevention)
      .def_readwrite("cancel_on_disconnect", &asterion::RiskLimits::cancel_on_disconnect)
      .def_readwrite("disconnect_order_policy",
                     &asterion::RiskLimits::disconnect_order_policy);

  py::class_<asterion::RiskResult>(module, "RiskResult")
      .def_readwrite("accepted", &asterion::RiskResult::accepted)
      .def_readwrite("reject_reason", &asterion::RiskResult::reject_reason);

  py::class_<asterion::RiskExposureSnapshot>(module, "RiskExposureSnapshot")
      .def_readwrite("positions", &asterion::RiskExposureSnapshot::positions)
      .def_readwrite("working_quantity", &asterion::RiskExposureSnapshot::working_quantity)
      .def_readwrite("working_order_count", &asterion::RiskExposureSnapshot::working_order_count)
      .def_readwrite("kill_switch_enabled", &asterion::RiskExposureSnapshot::kill_switch_enabled)
      .def_readwrite("connected", &asterion::RiskExposureSnapshot::connected)
      .def_readwrite("disconnect_count", &asterion::RiskExposureSnapshot::disconnect_count)
      .def_readwrite("disconnect_cancel_count",
                     &asterion::RiskExposureSnapshot::disconnect_cancel_count)
      .def_readwrite("rate_limit_mode", &asterion::RiskExposureSnapshot::rate_limit_mode)
      .def_readwrite("disconnect_order_policy",
                     &asterion::RiskExposureSnapshot::disconnect_order_policy)
      .def_readwrite("audit_entry_count", &asterion::RiskExposureSnapshot::audit_entry_count)
      .def_readwrite("audit_checksum", &asterion::RiskExposureSnapshot::audit_checksum);

  py::class_<asterion::RiskAuditEntry>(module, "RiskAuditEntry")
      .def_readwrite("timestamp_ns", &asterion::RiskAuditEntry::timestamp_ns)
      .def_readwrite("client_order_id", &asterion::RiskAuditEntry::client_order_id)
      .def_readwrite("symbol_id", &asterion::RiskAuditEntry::symbol_id)
      .def_readwrite("side", &asterion::RiskAuditEntry::side)
      .def_readwrite("accepted", &asterion::RiskAuditEntry::accepted)
      .def_readwrite("reject_reason", &asterion::RiskAuditEntry::reject_reason)
      .def_readwrite("check_name", &asterion::RiskAuditEntry::check_name)
      .def_readwrite("limit_value", &asterion::RiskAuditEntry::limit_value)
      .def_readwrite("observed_value", &asterion::RiskAuditEntry::observed_value);

  py::class_<asterion::RiskAuditTrail>(module, "RiskAuditTrail")
      .def("size", &asterion::RiskAuditTrail::size)
      .def("empty", &asterion::RiskAuditTrail::empty)
      .def("entries", &asterion::RiskAuditTrail::entries,
           py::return_value_policy::reference_internal)
      .def("checksum", &asterion::RiskAuditTrail::checksum)
      .def("accepted_count", &asterion::RiskAuditTrail::accepted_count)
      .def("rejected_count", &asterion::RiskAuditTrail::rejected_count);

  py::class_<asterion::RiskAuditVerificationResult>(module, "RiskAuditVerificationResult")
      .def_readwrite("valid", &asterion::RiskAuditVerificationResult::valid)
      .def_readwrite("files_checked", &asterion::RiskAuditVerificationResult::files_checked)
      .def_readwrite("entries_checked", &asterion::RiskAuditVerificationResult::entries_checked)
      .def_readwrite("final_checksum", &asterion::RiskAuditVerificationResult::final_checksum)
      .def_readwrite("error", &asterion::RiskAuditVerificationResult::error);

  py::class_<asterion::RiskGateway>(module, "RiskGateway")
      .def(py::init<asterion::RiskLimits>(), py::arg_v("limits", asterion::RiskLimits{},
                                                       "RiskLimits()"))
      .def("set_limits", &asterion::RiskGateway::set_limits)
      .def("limits", &asterion::RiskGateway::limits,
           py::return_value_policy::reference_internal)
      .def("enable_kill_switch",
           py::overload_cast<asterion::TimestampNs>(&asterion::RiskGateway::enable_kill_switch),
           py::arg("timestamp_ns") = 0)
      .def("disable_kill_switch", &asterion::RiskGateway::disable_kill_switch)
      .def("kill_switch_enabled", &asterion::RiskGateway::kill_switch_enabled)
      .def("on_disconnect", &asterion::RiskGateway::on_disconnect)
      .def("on_reconnect", &asterion::RiskGateway::on_reconnect, py::arg("timestamp_ns") = 0)
      .def("connected", &asterion::RiskGateway::connected)
      .def("on_market_data", &asterion::RiskGateway::on_market_data)
      .def("set_position", &asterion::RiskGateway::set_position)
      .def("position", &asterion::RiskGateway::position)
      .def("check_new_order", &asterion::RiskGateway::check_new_order)
      .def("check_replace_order", &asterion::RiskGateway::check_replace_order)
      .def("release_order", &asterion::RiskGateway::release_order)
      .def("on_execution_report", &asterion::RiskGateway::on_execution_report)
      .def("working_quantity", &asterion::RiskGateway::working_quantity)
      .def("exposure_snapshot", &asterion::RiskGateway::exposure_snapshot)
      .def("set_audit_enabled", &asterion::RiskGateway::set_audit_enabled)
      .def("audit_enabled", &asterion::RiskGateway::audit_enabled)
      .def("audit", &asterion::RiskGateway::audit, py::return_value_policy::reference_internal)
      .def("clear_audit", &asterion::RiskGateway::clear_audit)
      .def("open_audit_log", &asterion::RiskGateway::open_audit_log,
           py::arg("path"), py::arg("format") = asterion::RiskAuditLogFormat::Jsonl)
      .def("open_rotating_audit_log", &asterion::RiskGateway::open_rotating_audit_log,
           py::arg("path"), py::arg("format") = asterion::RiskAuditLogFormat::Jsonl,
           py::arg("max_records_per_file") = 0, py::arg("max_bytes_per_file") = 0)
      .def("close_audit_log", &asterion::RiskGateway::close_audit_log)
      .def("audit_log_enabled", &asterion::RiskGateway::audit_log_enabled)
      .def("audit_log_paths", &asterion::RiskGateway::audit_log_paths,
           py::return_value_policy::reference_internal);

  // ---- Inference backend status (ONNX) ----
  py::enum_<asterion::InferenceBackend>(module, "InferenceBackend")
      .value("Linear", asterion::InferenceBackend::Linear)
      .value("Onnx", asterion::InferenceBackend::Onnx);

  module.attr("onnx_runtime_available") = asterion::kOnnxRuntimeAvailable;
  module.def("inference_backend_to_string", [](asterion::InferenceBackend backend) {
    return std::string(asterion::to_string(backend));
  });
  module.def(
      "inference_backend_status",
      [](asterion::InferenceBackend requested) {
        asterion::InferenceBackendConfig config;
        config.requested = requested;
        config.linear_weights = {1.0, 0.0, 0.0, 0.0};
        config.linear_bias = 0.0;
        const asterion::InferenceBackendSelection selection =
            asterion::make_inference_backend(std::move(config));
        py::dict result;
        result["requested"] = std::string(asterion::to_string(selection.requested));
        result["active"] = std::string(asterion::to_string(selection.active));
        result["fell_back"] = selection.fell_back;
        result["detail"] = selection.detail;
        result["onnx_runtime_available"] = asterion::kOnnxRuntimeAvailable;
        if (selection.model != nullptr) {
          result["model_name"] = std::string(selection.model->model_name());
          result["input_shape"] = std::string(selection.model->input_shape());
          result["output_shape"] = std::string(selection.model->output_shape());
        }
        return result;
      },
      py::arg("requested") = asterion::InferenceBackend::Onnx);

  // ---- Shared replay parity ----
  py::class_<asterion::SymbolParityEntry>(module, "SymbolParityEntry")
      .def_readwrite("symbol_id", &asterion::SymbolParityEntry::symbol_id)
      .def_readwrite("present_in_grouped", &asterion::SymbolParityEntry::present_in_grouped)
      .def_readwrite("present_in_shared", &asterion::SymbolParityEntry::present_in_shared)
      .def_readwrite("event_log_checksum_match",
                     &asterion::SymbolParityEntry::event_log_checksum_match)
      .def_readwrite("final_book_checksum_match",
                     &asterion::SymbolParityEntry::final_book_checksum_match)
      .def_readwrite("execution_report_checksum_match",
                     &asterion::SymbolParityEntry::execution_report_checksum_match)
      .def_readwrite("diagnostics_checksum_match",
                     &asterion::SymbolParityEntry::diagnostics_checksum_match)
      .def_readwrite("matched", &asterion::SymbolParityEntry::matched);

  py::class_<asterion::ReplayParityReport>(module, "ReplayParityReport")
      .def_readwrite("matched", &asterion::ReplayParityReport::matched)
      .def_readwrite("symbol_count_grouped", &asterion::ReplayParityReport::symbol_count_grouped)
      .def_readwrite("symbol_count_shared", &asterion::ReplayParityReport::symbol_count_shared)
      .def_readwrite("mismatch_count", &asterion::ReplayParityReport::mismatch_count)
      .def_readwrite("combined_book_checksum_match",
                     &asterion::ReplayParityReport::combined_book_checksum_match)
      .def_readwrite("aggregate_checksum_match",
                     &asterion::ReplayParityReport::aggregate_checksum_match)
      .def_readwrite("grouped_combined_book_checksum",
                     &asterion::ReplayParityReport::grouped_combined_book_checksum)
      .def_readwrite("shared_combined_book_checksum",
                     &asterion::ReplayParityReport::shared_combined_book_checksum)
      .def_readwrite("grouped_aggregate_checksum",
                     &asterion::ReplayParityReport::grouped_aggregate_checksum)
      .def_readwrite("shared_aggregate_checksum",
                     &asterion::ReplayParityReport::shared_aggregate_checksum)
      .def_readwrite("symbols", &asterion::ReplayParityReport::symbols);

  module.def(
      "compare_replay_parity",
      [](const std::vector<asterion::MarketDataEvent>& events,
         asterion::AggregateReplayConfig config) {
        return asterion::compare_replay_parity(events, config);
      },
      py::arg("events"),
      py::arg_v("config", asterion::AggregateReplayConfig{}, "AggregateReplayConfig()"));
  module.def("compare_replay_parity_file", &asterion::compare_replay_parity_file, py::arg("path"),
             py::arg("format") = asterion::EventLogFormat::Auto,
             py::arg_v("config", asterion::AggregateReplayConfig{}, "AggregateReplayConfig()"));
  module.def(
      "describe_replay_parity",
      [](const std::vector<asterion::MarketDataEvent>& events,
         asterion::AggregateReplayConfig config) {
        return asterion::describe_replay_parity(events, config);
      },
      py::arg("events"),
      py::arg_v("config", asterion::AggregateReplayConfig{}, "AggregateReplayConfig()"));

  module.def(
      "run_spsc_replay",
      [](const std::vector<asterion::MarketDataEvent>& events, asterion::SymbolId symbol_id,
         asterion::SpscReplayConfig config) {
        // The C++ pipeline borrows the events as a span for the duration of the
        // call; the GIL is released so the producer/consumer threads run freely.
        py::gil_scoped_release release;
        return asterion::run_spsc_replay(events, symbol_id, config);
      },
      py::arg("events"), py::arg("symbol_id"),
      py::arg_v("config", asterion::SpscReplayConfig{}, "SpscReplayConfig()"));
  module.def(
      "run_spsc_replay_steady_state",
      [](const std::vector<asterion::MarketDataEvent>& events, asterion::SymbolId symbol_id,
         asterion::SpscReplayConfig config) {
        py::gil_scoped_release release;
        return asterion::run_spsc_replay_steady_state(events, symbol_id, config);
      },
      py::arg("events"), py::arg("symbol_id"),
      py::arg_v("config", asterion::SpscReplayConfig{}, "SpscReplayConfig()"));

  // ---- Tamper-evident audit manifests ----
  py::enum_<asterion::AuditManifestIssueType>(module, "AuditManifestIssueType")
      .value("None_", asterion::AuditManifestIssueType::None)
      .value("MissingFile", asterion::AuditManifestIssueType::MissingFile)
      .value("SizeMismatch", asterion::AuditManifestIssueType::SizeMismatch)
      .value("RecordCountMismatch", asterion::AuditManifestIssueType::RecordCountMismatch)
      .value("ContentChecksumMismatch", asterion::AuditManifestIssueType::ContentChecksumMismatch)
      .value("AuditChainMismatch", asterion::AuditManifestIssueType::AuditChainMismatch)
      .value("ManifestChainMismatch", asterion::AuditManifestIssueType::ManifestChainMismatch)
      .value("SignatureMismatch", asterion::AuditManifestIssueType::SignatureMismatch)
      .value("SignatureMissing", asterion::AuditManifestIssueType::SignatureMissing)
      .value("KeyMissing", asterion::AuditManifestIssueType::KeyMissing);

  py::class_<asterion::AuditManifestFileEntry>(module, "AuditManifestFileEntry")
      .def_readwrite("file_name", &asterion::AuditManifestFileEntry::file_name)
      .def_readwrite("record_count", &asterion::AuditManifestFileEntry::record_count)
      .def_readwrite("byte_size", &asterion::AuditManifestFileEntry::byte_size)
      .def_readwrite("content_checksum", &asterion::AuditManifestFileEntry::content_checksum)
      .def_readwrite("audit_chain_checksum",
                     &asterion::AuditManifestFileEntry::audit_chain_checksum);

  py::class_<asterion::AuditManifestSignature>(module, "AuditManifestSignature")
      .def_readwrite("algorithm", &asterion::AuditManifestSignature::algorithm)
      .def_readwrite("key_id", &asterion::AuditManifestSignature::key_id)
      .def_readwrite("value_hex", &asterion::AuditManifestSignature::value_hex);

  py::class_<asterion::AuditManifest>(module, "AuditManifest")
      .def_readwrite("schema_version", &asterion::AuditManifest::schema_version)
      .def_readwrite("creator", &asterion::AuditManifest::creator)
      .def_readwrite("created_at", &asterion::AuditManifest::created_at)
      .def_readwrite("format", &asterion::AuditManifest::format)
      .def_readwrite("files", &asterion::AuditManifest::files)
      .def_readwrite("chain_checksum", &asterion::AuditManifest::chain_checksum)
      .def_readwrite("signature", &asterion::AuditManifest::signature);

  py::class_<asterion::AuditManifestResult>(module, "AuditManifestResult")
      .def_readwrite("ok", &asterion::AuditManifestResult::ok)
      .def_readwrite("manifest", &asterion::AuditManifestResult::manifest)
      .def_readwrite("error", &asterion::AuditManifestResult::error);

  py::class_<asterion::AuditManifestIssue>(module, "AuditManifestIssue")
      .def_readwrite("type", &asterion::AuditManifestIssue::type)
      .def_readwrite("file_name", &asterion::AuditManifestIssue::file_name)
      .def_readwrite("detail", &asterion::AuditManifestIssue::detail);

  py::class_<asterion::AuditManifestVerification>(module, "AuditManifestVerification")
      .def_readwrite("valid", &asterion::AuditManifestVerification::valid)
      .def_readwrite("files_checked", &asterion::AuditManifestVerification::files_checked)
      .def_readwrite("entries_checked", &asterion::AuditManifestVerification::entries_checked)
      .def_readwrite("computed_chain_checksum",
                     &asterion::AuditManifestVerification::computed_chain_checksum)
      .def_readwrite("signature_present", &asterion::AuditManifestVerification::signature_present)
      .def_readwrite("signature_valid", &asterion::AuditManifestVerification::signature_valid)
      .def_readwrite("issues", &asterion::AuditManifestVerification::issues);

  module.def("audit_manifest_issue_type_to_string", [](asterion::AuditManifestIssueType type) {
    return std::string(asterion::to_string(type));
  });
  module.def(
      "generate_audit_manifest",
      [](const std::vector<std::filesystem::path>& paths, asterion::RiskAuditLogFormat format,
         const std::string& creator, const std::string& created_at, py::bytes signing_key,
         const std::string& signing_key_id) {
        asterion::AuditManifestGenerateOptions options;
        options.format = format;
        options.creator = creator;
        options.created_at = created_at;
        options.signing_key_id = signing_key_id;
        const std::string key = signing_key;
        options.signing_key.assign(key.begin(), key.end());
        return asterion::generate_audit_manifest(paths, std::move(options));
      },
      py::arg("paths"), py::arg("format") = asterion::RiskAuditLogFormat::Jsonl,
      py::arg("creator") = "asterion", py::arg("created_at") = "",
      py::arg("signing_key") = py::bytes(""), py::arg("signing_key_id") = "");
  module.def("serialize_audit_manifest", &asterion::serialize_audit_manifest);
  module.def("write_audit_manifest", &asterion::write_audit_manifest, py::arg("path"),
             py::arg("manifest"));
  module.def("read_audit_manifest", [](const std::filesystem::path& path) {
    std::string error;
    auto manifest = asterion::read_audit_manifest(path, &error);
    if (!manifest) {
      throw std::runtime_error(error);
    }
    return *manifest;
  });
  module.def("parse_audit_manifest", [](const std::string& text) {
    std::string error;
    auto manifest = asterion::parse_audit_manifest(text, &error);
    if (!manifest) {
      throw std::runtime_error(error);
    }
    return *manifest;
  });
  module.def(
      "verify_audit_manifest",
      [](const asterion::AuditManifest& manifest, const std::filesystem::path& base_dir,
         py::bytes signing_key) {
        asterion::AuditManifestVerifyOptions options;
        options.base_dir = base_dir;
        const std::string key = signing_key;
        options.signing_key.assign(key.begin(), key.end());
        return asterion::verify_audit_manifest(manifest, std::move(options));
      },
      py::arg("manifest"), py::arg("base_dir") = std::filesystem::path(),
      py::arg("signing_key") = py::bytes(""));

  // ---- Simulated broker/session lifecycle ----
  py::enum_<asterion::SessionConnectionState>(module, "SessionConnectionState")
      .value("Disconnected", asterion::SessionConnectionState::Disconnected)
      .value("Connected", asterion::SessionConnectionState::Connected);
  py::enum_<asterion::BrokerOrderState>(module, "BrokerOrderState")
      .value("Accepted", asterion::BrokerOrderState::Accepted)
      .value("PendingCancel", asterion::BrokerOrderState::PendingCancel)
      .value("Canceled", asterion::BrokerOrderState::Canceled)
      .value("Filled", asterion::BrokerOrderState::Filled);
  py::enum_<asterion::BrokerEventType>(module, "BrokerEventType")
      .value("Connect", asterion::BrokerEventType::Connect)
      .value("Disconnect", asterion::BrokerEventType::Disconnect)
      .value("Reconnect", asterion::BrokerEventType::Reconnect)
      .value("OrderAccepted", asterion::BrokerEventType::OrderAccepted)
      .value("CancelRequested", asterion::BrokerEventType::CancelRequested)
      .value("CancelAcknowledged", asterion::BrokerEventType::CancelAcknowledged)
      .value("CancelRejected", asterion::BrokerEventType::CancelRejected)
      .value("OrderFilled", asterion::BrokerEventType::OrderFilled);

  py::class_<asterion::BrokerSessionEvent>(module, "BrokerSessionEvent")
      .def_readwrite("timestamp_ns", &asterion::BrokerSessionEvent::timestamp_ns)
      .def_readwrite("type", &asterion::BrokerSessionEvent::type)
      .def_readwrite("exchange_order_id", &asterion::BrokerSessionEvent::exchange_order_id)
      .def_readwrite("client_order_id", &asterion::BrokerSessionEvent::client_order_id)
      .def_readwrite("symbol_id", &asterion::BrokerSessionEvent::symbol_id)
      .def_readwrite("side", &asterion::BrokerSessionEvent::side)
      .def_readwrite("quantity", &asterion::BrokerSessionEvent::quantity);

  py::class_<asterion::BrokerSessionSnapshot>(module, "BrokerSessionSnapshot")
      .def_readwrite("connection_state", &asterion::BrokerSessionSnapshot::connection_state)
      .def_readwrite("connect_count", &asterion::BrokerSessionSnapshot::connect_count)
      .def_readwrite("disconnect_count", &asterion::BrokerSessionSnapshot::disconnect_count)
      .def_readwrite("reconnect_count", &asterion::BrokerSessionSnapshot::reconnect_count)
      .def_readwrite("live_order_count", &asterion::BrokerSessionSnapshot::live_order_count)
      .def_readwrite("pending_cancel_count", &asterion::BrokerSessionSnapshot::pending_cancel_count)
      .def_readwrite("filled_order_count", &asterion::BrokerSessionSnapshot::filled_order_count)
      .def_readwrite("canceled_order_count", &asterion::BrokerSessionSnapshot::canceled_order_count)
      .def_readwrite("cancel_reject_count", &asterion::BrokerSessionSnapshot::cancel_reject_count)
      .def_readwrite("event_count", &asterion::BrokerSessionSnapshot::event_count)
      .def_readwrite("event_checksum", &asterion::BrokerSessionSnapshot::event_checksum);

  py::class_<asterion::SimulatedBrokerSession>(module, "SimulatedBrokerSession")
      .def(py::init<>())
      .def(py::init<bool>(), py::arg("cancel_on_disconnect"))
      .def("attach_risk_gateway", &asterion::SimulatedBrokerSession::attach_risk_gateway,
           py::keep_alive<1, 2>())
      .def("set_cancel_on_disconnect",
           &asterion::SimulatedBrokerSession::set_cancel_on_disconnect)
      .def("cancel_on_disconnect", &asterion::SimulatedBrokerSession::cancel_on_disconnect)
      .def("connect", &asterion::SimulatedBrokerSession::connect)
      .def("disconnect", &asterion::SimulatedBrokerSession::disconnect)
      .def("reconnect", &asterion::SimulatedBrokerSession::reconnect)
      .def("on_order_accepted", &asterion::SimulatedBrokerSession::on_order_accepted)
      .def("request_cancel", &asterion::SimulatedBrokerSession::request_cancel)
      .def("acknowledge_cancel", &asterion::SimulatedBrokerSession::acknowledge_cancel)
      .def("reject_cancel", &asterion::SimulatedBrokerSession::reject_cancel)
      .def("on_order_filled", &asterion::SimulatedBrokerSession::on_order_filled)
      .def("connection_state", &asterion::SimulatedBrokerSession::connection_state)
      .def("connected", &asterion::SimulatedBrokerSession::connected)
      .def("pending_cancel_count", &asterion::SimulatedBrokerSession::pending_cancel_count)
      .def("live_order_count", &asterion::SimulatedBrokerSession::live_order_count)
      .def("has_order", &asterion::SimulatedBrokerSession::has_order)
      .def("order_state", &asterion::SimulatedBrokerSession::order_state)
      .def("events", &asterion::SimulatedBrokerSession::events,
           py::return_value_policy::reference_internal)
      .def("event_checksum", &asterion::SimulatedBrokerSession::event_checksum)
      .def("snapshot", &asterion::SimulatedBrokerSession::snapshot);

  module.def("session_connection_state_to_string", [](asterion::SessionConnectionState state) {
    return std::string(asterion::to_string(state));
  });
  module.def("broker_order_state_to_string", [](asterion::BrokerOrderState state) {
    return std::string(asterion::to_string(state));
  });
  module.def("broker_event_type_to_string", [](asterion::BrokerEventType type) {
    return std::string(asterion::to_string(type));
  });

  // ---- Portfolio-level risk ----
  py::enum_<asterion::PortfolioBreach>(module, "PortfolioBreach")
      .value("None_", asterion::PortfolioBreach::None)
      .value("GrossExposure", asterion::PortfolioBreach::GrossExposure)
      .value("NetExposure", asterion::PortfolioBreach::NetExposure)
      .value("Concentration", asterion::PortfolioBreach::Concentration)
      .value("Loss", asterion::PortfolioBreach::Loss);

  py::class_<asterion::PortfolioRiskLimits>(module, "PortfolioRiskLimits")
      .def(py::init<>())
      .def_readwrite("max_gross_exposure_ticks",
                     &asterion::PortfolioRiskLimits::max_gross_exposure_ticks)
      .def_readwrite("max_net_exposure_ticks",
                     &asterion::PortfolioRiskLimits::max_net_exposure_ticks)
      .def_readwrite("max_symbol_concentration_bps",
                     &asterion::PortfolioRiskLimits::max_symbol_concentration_bps)
      .def_readwrite("max_loss_ticks", &asterion::PortfolioRiskLimits::max_loss_ticks);

  py::class_<asterion::PortfolioCheckResult>(module, "PortfolioCheckResult")
      .def_readwrite("accepted", &asterion::PortfolioCheckResult::accepted)
      .def_readwrite("breach", &asterion::PortfolioCheckResult::breach)
      .def_readwrite("symbol_id", &asterion::PortfolioCheckResult::symbol_id)
      .def_readwrite("limit_value", &asterion::PortfolioCheckResult::limit_value)
      .def_readwrite("observed_value", &asterion::PortfolioCheckResult::observed_value);

  py::class_<asterion::PortfolioSnapshot>(module, "PortfolioSnapshot")
      .def_readwrite("gross_exposure_ticks", &asterion::PortfolioSnapshot::gross_exposure_ticks)
      .def_readwrite("net_exposure_ticks", &asterion::PortfolioSnapshot::net_exposure_ticks)
      .def_readwrite("realised_pnl_ticks", &asterion::PortfolioSnapshot::realised_pnl_ticks)
      .def_readwrite("unrealised_pnl_ticks", &asterion::PortfolioSnapshot::unrealised_pnl_ticks)
      .def_readwrite("total_pnl_ticks", &asterion::PortfolioSnapshot::total_pnl_ticks)
      .def_readwrite("max_concentration_symbol",
                     &asterion::PortfolioSnapshot::max_concentration_symbol)
      .def_readwrite("max_concentration_bps", &asterion::PortfolioSnapshot::max_concentration_bps)
      .def_readwrite("symbol_count", &asterion::PortfolioSnapshot::symbol_count)
      .def_readwrite("checksum", &asterion::PortfolioSnapshot::checksum);

  py::class_<asterion::PortfolioRiskMonitor>(module, "PortfolioRiskMonitor")
      .def(py::init<>())
      .def(py::init<asterion::PortfolioRiskLimits>())
      .def("set_limits", &asterion::PortfolioRiskMonitor::set_limits)
      .def("limits", &asterion::PortfolioRiskMonitor::limits,
           py::return_value_policy::reference_internal)
      .def("active", &asterion::PortfolioRiskMonitor::active)
      .def("set_mark", &asterion::PortfolioRiskMonitor::set_mark)
      .def("apply_fill", &asterion::PortfolioRiskMonitor::apply_fill)
      .def("set_position", &asterion::PortfolioRiskMonitor::set_position)
      .def("position", &asterion::PortfolioRiskMonitor::position)
      .def("gross_exposure_ticks", &asterion::PortfolioRiskMonitor::gross_exposure_ticks)
      .def("net_exposure_ticks", &asterion::PortfolioRiskMonitor::net_exposure_ticks)
      .def("realised_pnl_ticks", &asterion::PortfolioRiskMonitor::realised_pnl_ticks)
      .def("unrealised_pnl_ticks", &asterion::PortfolioRiskMonitor::unrealised_pnl_ticks)
      .def("total_pnl_ticks", &asterion::PortfolioRiskMonitor::total_pnl_ticks)
      .def("evaluate_order", &asterion::PortfolioRiskMonitor::evaluate_order)
      .def("evaluate_state", &asterion::PortfolioRiskMonitor::evaluate_state)
      .def("check_order", &asterion::PortfolioRiskMonitor::check_order)
      .def("set_audit_enabled", &asterion::PortfolioRiskMonitor::set_audit_enabled)
      .def("audit_enabled", &asterion::PortfolioRiskMonitor::audit_enabled)
      .def("audit", &asterion::PortfolioRiskMonitor::audit,
           py::return_value_policy::reference_internal)
      .def("clear_audit", &asterion::PortfolioRiskMonitor::clear_audit)
      .def("snapshot", &asterion::PortfolioRiskMonitor::snapshot);

  module.def("portfolio_breach_to_string", [](asterion::PortfolioBreach breach) {
    return std::string(asterion::to_string(breach));
  });

  module.def("parse_event_log_format", &parse_format_or_throw);
  module.def("event_log_format_to_string",
             [](asterion::EventLogFormat format) { return std::string(asterion::to_string(format)); });
  module.def("market_event_type_to_string", [](asterion::MarketEventType type) {
    return std::string(asterion::to_string(type));
  });
  module.def("side_to_string",
             [](asterion::Side side) { return std::string(asterion::to_string(side)); });
  module.def("rate_limit_mode_to_string", [](asterion::RateLimitMode mode) {
    return std::string(asterion::to_string(mode));
  });
  module.def("disconnect_order_policy_to_string", [](asterion::DisconnectOrderPolicy policy) {
    return std::string(asterion::to_string(policy));
  });
  module.def("risk_audit_log_format_to_string", [](asterion::RiskAuditLogFormat format) {
    return std::string(asterion::to_string(format));
  });
  module.def("diagnostic_severity_to_string",
             [](asterion::ReplayDiagnosticSeverity severity) {
               return std::string(asterion::to_string(severity));
             });
  module.def("inference_decision_to_string", [](asterion::InferenceDecision decision) {
    return std::string(asterion::to_string(decision));
  });

  module.def("detect_event_log_format",
             [](const std::filesystem::path& path) {
               std::string error;
               const asterion::EventLogFormat format =
                   asterion::detect_event_log_format(path, &error);
               if (!error.empty()) {
                 throw std::runtime_error(error);
               }
               return format;
             });
  module.def("choose_event_log_format_for_path", &asterion::choose_event_log_format_for_path);
  module.def("read_event_log", &asterion::read_event_log, py::arg("path"),
             py::arg("format") = asterion::EventLogFormat::Auto);
  module.def("write_event_log",
             [](const std::filesystem::path& path,
                const std::vector<asterion::MarketDataEvent>& events,
                asterion::EventLogFormat format) {
               return asterion::write_event_log(path, events, format);
             },
             py::arg("path"), py::arg("events"), py::arg("format"));
  module.def("checksum_events", [](const std::vector<asterion::MarketDataEvent>& events) {
    return asterion::checksum_events(events);
  });
  module.def("market_data_event_to_csv", &asterion::market_data_event_to_csv);

  module.def("replay_file", &replay_file_for_symbol, py::arg("symbol_id"), py::arg("path"),
             py::arg("format") = asterion::EventLogFormat::Auto,
             py::arg_v("config", asterion::ReplayConfig{}, "ReplayConfig()"));
  module.def("replay_events", &replay_events_for_symbol, py::arg("symbol_id"),
             py::arg("events"),
             py::arg_v("config", asterion::ReplayConfig{}, "ReplayConfig()"));
  module.def("final_book_checksum",
             [](asterion::SymbolId symbol_id, const std::filesystem::path& path,
                asterion::EventLogFormat format) {
               return replay_file_for_symbol(symbol_id, path, format, asterion::ReplayConfig{})
                   .final_book_checksum;
             },
             py::arg("symbol_id"), py::arg("path"),
             py::arg("format") = asterion::EventLogFormat::Auto);
  module.def("execution_report_checksum",
             [](asterion::SymbolId symbol_id, const std::filesystem::path& path,
                asterion::EventLogFormat format) {
               return replay_file_for_symbol(symbol_id, path, format, asterion::ReplayConfig{})
                   .execution_report_checksum;
             },
             py::arg("symbol_id"), py::arg("path"),
             py::arg("format") = asterion::EventLogFormat::Auto);
  module.def("diagnostics_checksum",
             [](asterion::SymbolId symbol_id, const std::filesystem::path& path,
                asterion::EventLogFormat format) {
               return replay_file_for_symbol(symbol_id, path, format, asterion::ReplayConfig{})
                   .diagnostics_checksum;
             },
             py::arg("symbol_id"), py::arg("path"),
             py::arg("format") = asterion::EventLogFormat::Auto);
  module.def("checksum_diagnostics",
             [](const std::vector<asterion::ReplayDiagnostic>& diagnostics) {
               return asterion::checksum_diagnostics(diagnostics);
             });
  module.def("checksum_execution_reports",
             [](const std::vector<asterion::ExecutionReport>& reports) {
               return asterion::checksum_execution_reports(reports);
             });
  module.def("verify_risk_audit_logs",
             [](const std::vector<std::filesystem::path>& paths,
                asterion::RiskAuditLogFormat format) {
               return asterion::verify_risk_audit_logs(paths, format);
             },
             py::arg("paths"), py::arg("format") = asterion::RiskAuditLogFormat::Jsonl);

  module.def("replay_by_symbol",
             [](const std::vector<asterion::MarketDataEvent>& events,
                asterion::AggregateReplayConfig config) {
               return asterion::replay_by_symbol(events, config);
             },
             py::arg("events"),
             py::arg_v("config", asterion::AggregateReplayConfig{},
                       "AggregateReplayConfig()"));
  module.def("replay_file_by_symbol", &asterion::replay_file_by_symbol, py::arg("path"),
             py::arg("format") = asterion::EventLogFormat::Auto,
             py::arg_v("config", asterion::AggregateReplayConfig{},
                       "AggregateReplayConfig()"));
  module.def("replay_shared_by_symbol",
             [](const std::vector<asterion::MarketDataEvent>& events,
                asterion::AggregateReplayConfig config) {
               return asterion::replay_shared_by_symbol(events, config);
             },
             py::arg("events"),
             py::arg_v("config", asterion::AggregateReplayConfig{},
                       "AggregateReplayConfig()"));
  module.def("replay_file_shared_by_symbol", &asterion::replay_file_shared_by_symbol,
             py::arg("path"), py::arg("format") = asterion::EventLogFormat::Auto,
             py::arg_v("config", asterion::AggregateReplayConfig{},
                       "AggregateReplayConfig()"));

  module.def("evaluate_inference_policy", &asterion::evaluate_inference_policy,
             py::arg("policy"), py::arg("observed_latency_ns"),
             py::arg("signal_timestamp_ns") = 0, py::arg("now_timestamp_ns") = 0);
  module.def("measure_linear_inference",
             [](const asterion::LinearModel& model, const std::vector<double>& features,
                const asterion::InferencePolicy& policy,
                asterion::TimestampNs signal_timestamp_ns,
                asterion::TimestampNs now_timestamp_ns) {
               return asterion::measure_inference(model, features, policy, signal_timestamp_ns,
                                                  now_timestamp_ns);
             },
             py::arg("model"), py::arg("features"),
             py::arg_v("policy", asterion::InferencePolicy{}, "InferencePolicy()"),
             py::arg("signal_timestamp_ns") = 0, py::arg("now_timestamp_ns") = 0);
}
