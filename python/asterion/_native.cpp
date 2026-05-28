#include "asterion/book/order.hpp"
#include "asterion/book/order_book.hpp"
#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/inference/torchscript_model.hpp"
#include "asterion/market_data/event.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/market_data/replay_aggregate.hpp"
#include "asterion/matching/execution_report.hpp"
#include "asterion/risk/risk_gateway.hpp"

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
      .value("SelfTradePrevention", asterion::RejectReason::SelfTradePrevention);

  py::enum_<asterion::RateLimitMode>(module, "RateLimitMode")
      .value("FixedWindow", asterion::RateLimitMode::FixedWindow)
      .value("SlidingWindow", asterion::RateLimitMode::SlidingWindow);

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

  py::class_<asterion::ReplayConfig>(module, "ReplayConfig")
      .def(py::init<>())
      .def_readwrite("validate_sequence_numbers",
                     &asterion::ReplayConfig::validate_sequence_numbers)
      .def_readwrite("validate_timestamps", &asterion::ReplayConfig::validate_timestamps)
      .def_readwrite("validate_book_state", &asterion::ReplayConfig::validate_book_state)
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
      .def_readwrite("backend", &asterion::InferenceResult::backend);

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
      .def_readwrite("client_id", &asterion::NewOrderRequest::client_id);

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
                     &asterion::RiskLimits::enable_self_trade_prevention);

  py::class_<asterion::RiskResult>(module, "RiskResult")
      .def_readwrite("accepted", &asterion::RiskResult::accepted)
      .def_readwrite("reject_reason", &asterion::RiskResult::reject_reason);

  py::class_<asterion::RiskExposureSnapshot>(module, "RiskExposureSnapshot")
      .def_readwrite("positions", &asterion::RiskExposureSnapshot::positions)
      .def_readwrite("working_quantity", &asterion::RiskExposureSnapshot::working_quantity)
      .def_readwrite("working_order_count", &asterion::RiskExposureSnapshot::working_order_count)
      .def_readwrite("kill_switch_enabled", &asterion::RiskExposureSnapshot::kill_switch_enabled)
      .def_readwrite("rate_limit_mode", &asterion::RiskExposureSnapshot::rate_limit_mode)
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
      .def("on_market_data", &asterion::RiskGateway::on_market_data)
      .def("set_position", &asterion::RiskGateway::set_position)
      .def("position", &asterion::RiskGateway::position)
      .def("check_new_order", &asterion::RiskGateway::check_new_order)
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
      .def("close_audit_log", &asterion::RiskGateway::close_audit_log)
      .def("audit_log_enabled", &asterion::RiskGateway::audit_log_enabled);

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
