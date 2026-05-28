#include "asterion/market_data/replay_aggregate.hpp"

#include "asterion/core/checksum.hpp"
#include "asterion/market_data/multi_symbol_book.hpp"

#include <map>
#include <string>
#include <utility>

namespace asterion {

namespace {

[[nodiscard]] std::uint64_t append_summary_checksum(std::uint64_t seed,
                                                    const SymbolReplaySummary& summary) noexcept {
  seed = checksum_append(seed, summary.symbol_id);
  seed = checksum_append(seed, static_cast<std::uint64_t>(summary.event_count));
  seed = checksum_append(seed, summary.first_sequence);
  seed = checksum_append(seed, summary.last_sequence);
  seed = checksum_append(seed, summary.first_timestamp_ns);
  seed = checksum_append(seed, summary.last_timestamp_ns);
  seed = checksum_append(seed, static_cast<std::uint8_t>(summary.sequence_valid ? 1U : 0U));
  seed = checksum_append(seed, summary.event_log_checksum);
  seed = checksum_append(seed, summary.final_book_checksum);
  seed = checksum_append(seed, summary.execution_report_checksum);
  seed = checksum_append(seed, summary.diagnostics_checksum);
  seed = checksum_append(seed, static_cast<std::uint64_t>(summary.diagnostic_count));
  seed = checksum_append(seed, static_cast<std::uint64_t>(summary.diagnostic_error_count));
  seed = checksum_append(seed, static_cast<std::uint64_t>(summary.diagnostic_warning_count));
  seed = checksum_append_string(seed, summary.error);
  return seed;
}

[[nodiscard]] std::uint64_t checksum_aggregate(const AggregateReplaySummary& summary) noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  seed = checksum_append(seed, static_cast<std::uint64_t>(summary.total_events));
  seed = checksum_append(seed, static_cast<std::uint64_t>(summary.symbol_count));
  seed = checksum_append(seed, summary.combined_book_checksum);
  for (const SymbolReplaySummary& symbol_summary : summary.symbols) {
    seed = append_summary_checksum(seed, symbol_summary);
  }
  seed = checksum_append_string(seed, summary.error);
  return seed;
}

[[nodiscard]] std::uint64_t checksum_combined_books_from_summaries(
    const std::vector<SymbolReplaySummary>& summaries) noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  seed = checksum_append(seed, static_cast<std::uint64_t>(summaries.size()));
  for (const SymbolReplaySummary& summary : summaries) {
    seed = checksum_append(seed, summary.symbol_id);
    seed = checksum_append(seed, summary.final_book_checksum);
  }
  return seed;
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

[[nodiscard]] std::string apply_failure_reason(MarketEventType type) {
  switch (type) {
  case MarketEventType::Add:
    return "failed to apply Add event";
  case MarketEventType::Cancel:
    return "failed to apply Cancel event";
  case MarketEventType::Replace:
    return "failed to apply Replace event";
  case MarketEventType::Execute:
    return "failed to apply Execute event";
  case MarketEventType::Snapshot:
    return "failed to apply Snapshot event";
  case MarketEventType::Trade:
    return "failed to apply Trade event";
  case MarketEventType::Heartbeat:
    return "failed to apply Heartbeat event";
  }
  return "failed to apply event";
}

struct SharedSymbolState {
  SymbolReplaySummary summary;
  SequenceNumber expected_sequence{0};
  TimestampNs last_timestamp_ns{0};
  bool has_last_timestamp{false};
  bool failed{false};
};

void add_shared_diagnostic(SharedSymbolState& state, const MarketDataEvent& event,
                           ReplayDiagnosticSeverity severity, std::string reason) {
  ReplayDiagnostic diagnostic{state.summary.event_count - 1U, event.sequence_number,
                              event.symbol_id, severity, std::move(reason)};
  state.summary.diagnostics_checksum =
      append_to_checksum(state.summary.diagnostics_checksum, diagnostic);
  if (severity == ReplayDiagnosticSeverity::Error) {
    ++state.summary.diagnostic_error_count;
  } else if (severity == ReplayDiagnosticSeverity::Warning) {
    ++state.summary.diagnostic_warning_count;
  }
  state.summary.diagnostics.push_back(std::move(diagnostic));
}

void mark_shared_failure(SharedSymbolState& state) {
  state.summary.sequence_valid = false;
  state.failed = true;
  if (!state.summary.diagnostics.empty()) {
    state.summary.error = state.summary.diagnostics.back().reason;
  }
}

[[nodiscard]] bool validate_shared_event_before_apply(const MarketDataEvent& event,
                                                      const OrderBook* book,
                                                      SharedSymbolState& state) {
  if (event.symbol_id == kInvalidSymbolId) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, "invalid symbol id");
    return false;
  }
  if (is_order_id_required(event.event_type) && event.order_id == kInvalidOrderId) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, "invalid order id");
    return false;
  }
  if ((event.event_type == MarketEventType::Add ||
       event.event_type == MarketEventType::Replace) &&
      !is_valid_side(event.side)) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, "invalid side");
    return false;
  }
  if (is_price_required(event.event_type) && event.price_ticks <= 0) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, "invalid price");
    return false;
  }
  if (!is_price_required(event.event_type) && event.price_ticks < 0) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, "invalid negative price");
    return false;
  }
  if (is_quantity_required(event.event_type) && event.quantity <= 0) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, "invalid quantity");
    return false;
  }
  if (!is_quantity_required(event.event_type) && event.quantity < 0) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                          "invalid negative quantity");
    return false;
  }

  if (event.event_type == MarketEventType::Add && book != nullptr &&
      book->find_order(event.order_id) != nullptr) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, "duplicate order id");
    return false;
  }

  if (event.event_type == MarketEventType::Snapshot && event.order_id != kInvalidOrderId) {
    if (!is_valid_side(event.side)) {
      add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                            "invalid snapshot side");
      return false;
    }
    if (event.price_ticks <= 0) {
      add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                            "invalid snapshot price");
      return false;
    }
    if (event.quantity <= 0) {
      add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                            "invalid snapshot quantity");
      return false;
    }
  }

  if (event.event_type == MarketEventType::Cancel ||
      event.event_type == MarketEventType::Replace ||
      event.event_type == MarketEventType::Execute) {
    const Order* order = book == nullptr ? nullptr : book->find_order(event.order_id);
    if (order == nullptr) {
      std::string reason;
      if (event.event_type == MarketEventType::Cancel) {
        reason = "unknown cancel order id";
      } else if (event.event_type == MarketEventType::Replace) {
        reason = "unknown replace order id";
      } else {
        reason = "unknown execute order id";
      }
      add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, std::move(reason));
      return false;
    }
    if ((event.event_type == MarketEventType::Cancel ||
         event.event_type == MarketEventType::Execute) &&
        event.quantity > order->quantity) {
      add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                            "invalid quantity exceeds resting order quantity");
      return false;
    }
  }

  return true;
}

[[nodiscard]] bool validate_shared_book_after_apply(const MarketDataEvent& event,
                                                    const OrderBook& book,
                                                    const ReplayConfig& config,
                                                    SharedSymbolState& state) {
  if (!config.validate_book_state) {
    return true;
  }

  const auto invariant_report = book.check_invariants();
  if (!invariant_report.ok) {
    const std::string reason =
        invariant_report.violations.empty() ? "book invariant violation"
                                            : "book invariant violation: " +
                                                  invariant_report.violations.front();
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error, reason);
    return false;
  }

  const auto best_bid = book.best_bid();
  const auto best_ask = book.best_ask();
  if (best_bid.has_value() && best_ask.has_value() && *best_bid >= *best_ask) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                          "crossed book state: best bid " + std::to_string(*best_bid) +
                              " >= best ask " + std::to_string(*best_ask));
    return false;
  }

  return true;
}

} // namespace

AggregateReplaySummary replay_by_symbol(std::span<const MarketDataEvent> events,
                                        AggregateReplayConfig config) {
  AggregateReplaySummary aggregate;
  aggregate.total_events = events.size();

  std::map<SymbolId, std::vector<MarketDataEvent>> grouped_events;
  for (const MarketDataEvent& event : events) {
    grouped_events[event.symbol_id].push_back(event);
  }

  aggregate.symbols.reserve(grouped_events.size());
  for (const auto& [symbol_id, symbol_events] : grouped_events) {
    ReplayConfig replay_config = config.replay_config;
    if (!config.validate_per_symbol_sequences) {
      replay_config.validate_sequence_numbers = false;
    }

    ReplayEngine replay(symbol_id, replay_config);
    const ReplayResult replay_result = replay.replay_events(symbol_events);

    SymbolReplaySummary summary;
    summary.symbol_id = symbol_id;
    summary.event_count = symbol_events.size();
    if (!symbol_events.empty()) {
      summary.first_sequence = symbol_events.front().sequence_number;
      summary.last_sequence = symbol_events.back().sequence_number;
      summary.first_timestamp_ns = symbol_events.front().timestamp_ns;
      summary.last_timestamp_ns = symbol_events.back().timestamp_ns;
    }
    summary.sequence_valid = replay_result.sequence_valid;
    summary.event_log_checksum = replay_result.event_log_checksum;
    summary.final_book_checksum = replay_result.final_book_checksum;
    summary.execution_report_checksum = replay_result.execution_report_checksum;
    summary.diagnostics_checksum = replay_result.diagnostics_checksum;
    summary.diagnostic_count = replay_result.diagnostics.size();
    summary.diagnostic_error_count = replay_result.diagnostic_error_count;
    summary.diagnostic_warning_count = replay_result.diagnostic_warning_count;
    summary.diagnostics = replay_result.diagnostics;
    summary.error = replay_result.error;
    aggregate.symbols.push_back(std::move(summary));
  }

  aggregate.symbol_count = aggregate.symbols.size();
  aggregate.combined_book_checksum = checksum_combined_books_from_summaries(aggregate.symbols);
  aggregate.aggregate_checksum = checksum_aggregate(aggregate);
  return aggregate;
}

AggregateReplaySummary replay_file_by_symbol(const std::filesystem::path& path,
                                             EventLogFormat format,
                                             AggregateReplayConfig config) {
  EventLogReadResult read_result = read_event_log(path, format);
  if (!read_result.error.empty()) {
    AggregateReplaySummary aggregate;
    aggregate.error = read_result.error;
    aggregate.aggregate_checksum = checksum_aggregate(aggregate);
    return aggregate;
  }
  return replay_by_symbol(read_result.events, config);
}

AggregateReplaySummary replay_shared_by_symbol(std::span<const MarketDataEvent> events,
                                               AggregateReplayConfig config) {
  AggregateReplaySummary aggregate;
  aggregate.total_events = events.size();

  MultiSymbolBookSet book_set;
  std::map<SymbolId, SharedSymbolState> states;
  const bool validate_sequences =
      config.validate_per_symbol_sequences && config.replay_config.validate_sequence_numbers;

  for (const MarketDataEvent& event : events) {
    SharedSymbolState& state = states[event.symbol_id];
    SymbolReplaySummary& summary = state.summary;
    if (summary.event_count == 0) {
      summary.symbol_id = event.symbol_id;
      summary.first_sequence = event.sequence_number;
      summary.first_timestamp_ns = event.timestamp_ns;
      summary.event_log_checksum = kFnvOffsetBasis;
      summary.diagnostics_checksum = kFnvOffsetBasis;
      summary.execution_report_checksum = kFnvOffsetBasis;
    }
    ++summary.event_count;
    summary.last_sequence = event.sequence_number;
    summary.last_timestamp_ns = event.timestamp_ns;
    summary.event_log_checksum = append_to_checksum(summary.event_log_checksum, event);

    if (state.failed) {
      continue;
    }

    if (validate_sequences) {
      if (state.expected_sequence == 0) {
        state.expected_sequence = event.sequence_number;
      }
      if (event.sequence_number != state.expected_sequence) {
        add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                              "sequence gap: expected " +
                                  std::to_string(state.expected_sequence) + ", received " +
                                  std::to_string(event.sequence_number));
        mark_shared_failure(state);
        continue;
      }
      ++state.expected_sequence;
    }

    if (config.replay_config.validate_timestamps) {
      if (state.has_last_timestamp && event.timestamp_ns < state.last_timestamp_ns) {
        add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                              "timestamp reversal: previous " +
                                  std::to_string(state.last_timestamp_ns) + ", received " +
                                  std::to_string(event.timestamp_ns));
        mark_shared_failure(state);
        continue;
      }
      state.last_timestamp_ns = event.timestamp_ns;
      state.has_last_timestamp = true;
    }

    const OrderBook* book = book_set.find_book(event.symbol_id);
    if (!validate_shared_event_before_apply(event, book, state)) {
      mark_shared_failure(state);
      continue;
    }

    if (!book_set.apply(event)) {
      add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                            apply_failure_reason(event.event_type));
      mark_shared_failure(state);
      continue;
    }

    book = book_set.find_book(event.symbol_id);
    if (book == nullptr ||
        !validate_shared_book_after_apply(event, *book, config.replay_config, state)) {
      mark_shared_failure(state);
      continue;
    }

    if (event.event_type == MarketEventType::Execute ||
        event.event_type == MarketEventType::Trade) {
      summary.execution_report_checksum =
          append_to_checksum(summary.execution_report_checksum, event);
    }
  }

  aggregate.symbols.reserve(states.size());
  for (auto& [symbol_id, state] : states) {
    SymbolReplaySummary& summary = state.summary;
    const OrderBook* book = book_set.find_book(symbol_id);
    if (book != nullptr) {
      summary.final_book_checksum = book->checksum();
    } else {
      summary.final_book_checksum = OrderBook(symbol_id).checksum();
    }
    summary.diagnostic_count = summary.diagnostics.size();
    aggregate.symbols.push_back(std::move(summary));
  }

  aggregate.symbol_count = aggregate.symbols.size();
  aggregate.combined_book_checksum = checksum_combined_books_from_summaries(aggregate.symbols);
  if (book_set.symbol_count() == aggregate.symbol_count &&
      book_set.combined_checksum() != aggregate.combined_book_checksum) {
    aggregate.error = "shared replay combined checksum mismatch";
  }
  aggregate.aggregate_checksum = checksum_aggregate(aggregate);
  return aggregate;
}

AggregateReplaySummary replay_file_shared_by_symbol(const std::filesystem::path& path,
                                                    EventLogFormat format,
                                                    AggregateReplayConfig config) {
  EventLogReadResult read_result = read_event_log(path, format);
  if (!read_result.error.empty()) {
    AggregateReplaySummary aggregate;
    aggregate.error = read_result.error;
    aggregate.aggregate_checksum = checksum_aggregate(aggregate);
    return aggregate;
  }
  return replay_shared_by_symbol(read_result.events, config);
}

} // namespace asterion
