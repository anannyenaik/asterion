#include "asterion/market_data/replay.hpp"

#include "asterion/core/checksum.hpp"

#include <string>
#include <string_view>
#include <utility>

namespace asterion {

namespace {

void finalize_result(ReplayEngine& replay, ReplayResult& result) {
  result.final_book_checksum = replay.book().checksum();
}

[[nodiscard]] bool is_price_required(MarketEventType type) {
  return type == MarketEventType::Add || type == MarketEventType::Replace ||
         type == MarketEventType::Trade;
}

[[nodiscard]] bool is_quantity_required(MarketEventType type) {
  return type == MarketEventType::Add || type == MarketEventType::Replace ||
         type == MarketEventType::Execute || type == MarketEventType::Trade;
}

[[nodiscard]] bool is_order_id_required(MarketEventType type) {
  return type == MarketEventType::Add || type == MarketEventType::Cancel ||
         type == MarketEventType::Replace || type == MarketEventType::Execute;
}

} // namespace

std::string_view to_string(ReplayDiagnosticSeverity severity) noexcept {
  switch (severity) {
  case ReplayDiagnosticSeverity::Info:
    return "info";
  case ReplayDiagnosticSeverity::Warning:
    return "warning";
  case ReplayDiagnosticSeverity::Error:
    return "error";
  }
  return "unknown";
}

std::uint64_t append_to_checksum(std::uint64_t seed,
                                 const ReplayDiagnostic& diagnostic) noexcept {
  seed = checksum_append(seed, static_cast<std::uint64_t>(diagnostic.event_index));
  seed = checksum_append(seed, diagnostic.sequence_number);
  seed = checksum_append(seed, diagnostic.symbol_id);
  seed = checksum_append(seed, diagnostic.severity);
  seed = checksum_append_string(seed, diagnostic.reason);
  return seed;
}

std::uint64_t checksum_diagnostics(std::span<const ReplayDiagnostic> diagnostics) noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  for (const ReplayDiagnostic& diagnostic : diagnostics) {
    seed = append_to_checksum(seed, diagnostic);
  }
  return seed;
}

ReplayEngine::ReplayEngine(SymbolId symbol_id, ReplayConfig config)
    : config_(config), book_(symbol_id) {}

ReplayResult ReplayEngine::replay_file(const std::filesystem::path& path, EventLogFormat format) {
  ReplayResult result;
  result.execution_report_checksum = kFnvOffsetBasis;
  result.diagnostics_checksum = kFnvOffsetBasis;
  result.event_log_checksum = kFnvOffsetBasis;

  EventLogReadResult log = read_event_log(path, format);
  if (!log.error.empty()) {
    result.sequence_valid = false;
    result.error = log.error;
    return result;
  }
  result = replay_events(log.events);
  result.event_log_checksum = log.event_checksum;
  return result;
}

ReplayResult ReplayEngine::replay_events(std::span<const MarketDataEvent> events) {
  ReplayResult result;
  result.execution_report_checksum = kFnvOffsetBasis;
  result.diagnostics_checksum = kFnvOffsetBasis;
  result.event_log_checksum = checksum_events(events);
  SequenceNumber expected_sequence = 0;
  TimestampNs last_timestamp = 0;
  bool has_last_timestamp = false;

  for (std::size_t index = 0; index < events.size(); ++index) {
    const MarketDataEvent& event = events[index];
    if (config_.validate_sequence_numbers) {
      if (expected_sequence == 0) {
        expected_sequence = event.sequence_number;
      }
      if (event.sequence_number != expected_sequence) {
        add_diagnostic(result, index, event, ReplayDiagnosticSeverity::Error,
                       "sequence gap: expected " + std::to_string(expected_sequence) +
                           ", received " + std::to_string(event.sequence_number));
        result.sequence_valid = false;
        result.error = result.diagnostics.back().reason;
        finalize_result(*this, result);
        return result;
      }
      ++expected_sequence;
    }
    if (config_.validate_timestamps) {
      if (has_last_timestamp && event.timestamp_ns < last_timestamp) {
        add_diagnostic(result, index, event, ReplayDiagnosticSeverity::Error,
                       "timestamp reversal: previous " + std::to_string(last_timestamp) +
                           ", received " + std::to_string(event.timestamp_ns));
        result.sequence_valid = false;
        result.error = result.diagnostics.back().reason;
        finalize_result(*this, result);
        return result;
      }
      last_timestamp = event.timestamp_ns;
      has_last_timestamp = true;
    }

    if (!validate_event_before_apply(event, index, result)) {
      result.sequence_valid = false;
      result.error = result.diagnostics.back().reason;
      finalize_result(*this, result);
      return result;
    }

    if (!apply_event(event, index, result)) {
      result.sequence_valid = false;
      finalize_result(*this, result);
      return result;
    }
    ++result.events_processed;
  }

  finalize_result(*this, result);
  return result;
}

bool ReplayEngine::validate_event_before_apply(const MarketDataEvent& event,
                                               std::size_t event_index, ReplayResult& result) {
  if (event.symbol_id == kInvalidSymbolId) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "invalid symbol id");
    return false;
  }
  if (event.symbol_id != book_.symbol_id()) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "event symbol does not match replay book symbol");
    return false;
  }
  if (is_order_id_required(event.event_type) && event.order_id == kInvalidOrderId) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "invalid order id");
    return false;
  }
  if ((event.event_type == MarketEventType::Add ||
       event.event_type == MarketEventType::Replace) &&
      !is_valid_side(event.side)) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error, "invalid side");
    return false;
  }
  if (is_price_required(event.event_type) && event.price_ticks <= 0) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "invalid price");
    return false;
  }
  if (!is_price_required(event.event_type) && event.price_ticks < 0) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "invalid negative price");
    return false;
  }
  if (is_quantity_required(event.event_type) && event.quantity <= 0) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "invalid quantity");
    return false;
  }
  if (!is_quantity_required(event.event_type) && event.quantity < 0) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "invalid negative quantity");
    return false;
  }

  if (event.event_type == MarketEventType::Add && book_.find_order(event.order_id) != nullptr) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "duplicate order id");
    return false;
  }

  if (event.event_type == MarketEventType::Cancel ||
      event.event_type == MarketEventType::Replace ||
      event.event_type == MarketEventType::Execute) {
    const Order* order = book_.find_order(event.order_id);
    if (order == nullptr) {
      std::string reason;
      if (event.event_type == MarketEventType::Cancel) {
        reason = "unknown cancel order id";
      } else if (event.event_type == MarketEventType::Replace) {
        reason = "unknown replace order id";
      } else {
        reason = "unknown execute order id";
      }
      add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                     std::move(reason));
      return false;
    }
    if ((event.event_type == MarketEventType::Cancel ||
         event.event_type == MarketEventType::Execute) &&
        event.quantity > order->quantity) {
      add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                     "invalid quantity exceeds resting order quantity");
      return false;
    }
  }

  return true;
}

bool ReplayEngine::apply_event(const MarketDataEvent& event, std::size_t event_index,
                               ReplayResult& result) {
  switch (event.event_type) {
  case MarketEventType::Add: {
    const bool ok = book_.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                          event.side, event.price_ticks, event.quantity,
                                          event.timestamp_ns, event.sequence_number});
    if (!ok) {
      add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                     "failed to apply Add event");
      result.error = result.diagnostics.back().reason;
      return false;
    }
    break;
  }
  case MarketEventType::Cancel:
    if (event.quantity > 0) {
      if (!book_.reduce_order(event.order_id, event.quantity)) {
        add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                       "failed to apply partial Cancel event");
        result.error = result.diagnostics.back().reason;
        return false;
      }
    } else if (!book_.cancel_order(event.order_id)) {
      add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                     "failed to apply Cancel event");
      result.error = result.diagnostics.back().reason;
      return false;
    }
    break;
  case MarketEventType::Replace:
    if (!book_.replace_order(event.order_id, event.price_ticks, event.quantity, event.timestamp_ns,
                             event.sequence_number)) {
      add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                     "failed to apply Replace event");
      result.error = result.diagnostics.back().reason;
      return false;
    }
    break;
  case MarketEventType::Execute:
    if (!book_.reduce_order(event.order_id, event.quantity)) {
      add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                     "failed to apply Execute event");
      result.error = result.diagnostics.back().reason;
      return false;
    }
    update_activity_checksum(event, result);
    break;
  case MarketEventType::Trade:
    update_activity_checksum(event, result);
    break;
  case MarketEventType::Snapshot:
  case MarketEventType::Heartbeat:
    break;
  }

  return validate_book_after_apply(event, event_index, result);
}

bool ReplayEngine::validate_book_after_apply(const MarketDataEvent& event,
                                             std::size_t event_index, ReplayResult& result) {
  if (!config_.validate_book_state) {
    return true;
  }

  const auto invariant_report = book_.check_invariants();
  if (!invariant_report.ok) {
    const std::string reason =
        invariant_report.violations.empty() ? "book invariant violation"
                                            : "book invariant violation: " +
                                                  invariant_report.violations.front();
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error, reason);
    result.error = result.diagnostics.back().reason;
    return false;
  }

  const auto best_bid = book_.best_bid();
  const auto best_ask = book_.best_ask();
  if (best_bid.has_value() && best_ask.has_value() && *best_bid >= *best_ask) {
    add_diagnostic(result, event_index, event, ReplayDiagnosticSeverity::Error,
                   "crossed book state: best bid " + std::to_string(*best_bid) +
                       " >= best ask " + std::to_string(*best_ask));
    result.error = result.diagnostics.back().reason;
    return false;
  }

  return true;
}

void ReplayEngine::add_diagnostic(ReplayResult& result, std::size_t event_index,
                                  const MarketDataEvent& event,
                                  ReplayDiagnosticSeverity severity, std::string reason) const {
  ReplayDiagnostic diagnostic{event_index, event.sequence_number, event.symbol_id, severity,
                              std::move(reason)};
  result.diagnostics_checksum = append_to_checksum(result.diagnostics_checksum, diagnostic);
  if (severity == ReplayDiagnosticSeverity::Error) {
    ++result.diagnostic_error_count;
  } else if (severity == ReplayDiagnosticSeverity::Warning) {
    ++result.diagnostic_warning_count;
  }
  result.diagnostics.push_back(std::move(diagnostic));
}

void ReplayEngine::update_activity_checksum(const MarketDataEvent& event,
                                            ReplayResult& result) const {
  result.execution_report_checksum = append_to_checksum(result.execution_report_checksum, event);
}

} // namespace asterion
