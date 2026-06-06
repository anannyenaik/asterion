#include "asterion/market_data/replay_aggregate.hpp"

#include "asterion/core/checksum.hpp"
#include "asterion/market_data/multi_symbol_book.hpp"

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

[[nodiscard]] bool validate_shared_top_of_book(const MarketDataEvent& event,
                                               const OrderBook& book,
                                               SharedSymbolState& state) {
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

[[nodiscard]] bool validate_shared_full_book(const MarketDataEvent& event, const OrderBook& book,
                                             SharedSymbolState& state, std::string_view prefix) {
  const auto invariant_report = book.check_invariants();
  if (!invariant_report.ok) {
    const std::string reason =
        invariant_report.violations.empty() ? "book invariant violation"
                                            : "book invariant violation: " +
                                                  invariant_report.violations.front();
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                          std::string(prefix) + reason);
    return false;
  }

  return validate_shared_top_of_book(event, book, state);
}

[[nodiscard]] bool validate_shared_book_after_apply(const MarketDataEvent& event,
                                                    const OrderBook& book,
                                                    const ReplayConfig& config,
                                                    SharedSymbolState& state) {
  if (!config.validate_book_state) {
    return true;
  }
  if (config.validation_mode == ReplayValidationMode::Light) {
    return validate_shared_top_of_book(event, book, state);
  }
  if (config.validation_mode != ReplayValidationMode::Full) {
    add_shared_diagnostic(state, event, ReplayDiagnosticSeverity::Error,
                          "unsupported replay validation mode");
    return false;
  }
  return validate_shared_full_book(event, book, state, "");
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
      if (!state.failed && config.replay_config.validate_book_state &&
          config.replay_config.validation_mode == ReplayValidationMode::Light) {
        MarketDataEvent final_event{};
        final_event.symbol_id = symbol_id;
        if (!validate_shared_full_book(final_event, *book, state, "final ")) {
          mark_shared_failure(state);
        }
      }
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

ReplayParityReport compare_replay_parity(std::span<const MarketDataEvent> events,
                                         AggregateReplayConfig config) {
  const AggregateReplaySummary grouped = replay_by_symbol(events, config);
  const AggregateReplaySummary shared = replay_shared_by_symbol(events, config);

  ReplayParityReport report;
  report.symbol_count_grouped = grouped.symbol_count;
  report.symbol_count_shared = shared.symbol_count;
  report.grouped_combined_book_checksum = grouped.combined_book_checksum;
  report.shared_combined_book_checksum = shared.combined_book_checksum;
  report.combined_book_checksum_match =
      grouped.combined_book_checksum == shared.combined_book_checksum;
  report.grouped_aggregate_checksum = grouped.aggregate_checksum;
  report.shared_aggregate_checksum = shared.aggregate_checksum;
  report.aggregate_checksum_match = grouped.aggregate_checksum == shared.aggregate_checksum;

  std::map<SymbolId, const SymbolReplaySummary*> grouped_by_symbol;
  std::map<SymbolId, const SymbolReplaySummary*> shared_by_symbol;
  for (const SymbolReplaySummary& summary : grouped.symbols) {
    grouped_by_symbol[summary.symbol_id] = &summary;
  }
  for (const SymbolReplaySummary& summary : shared.symbols) {
    shared_by_symbol[summary.symbol_id] = &summary;
  }
  std::map<SymbolId, bool> all_symbols;
  for (const auto& item : grouped_by_symbol) {
    all_symbols[item.first] = true;
  }
  for (const auto& item : shared_by_symbol) {
    all_symbols[item.first] = true;
  }

  for (const auto& item : all_symbols) {
    const SymbolId symbol_id = item.first;
    SymbolParityEntry entry;
    entry.symbol_id = symbol_id;
    const auto grouped_it = grouped_by_symbol.find(symbol_id);
    const auto shared_it = shared_by_symbol.find(symbol_id);
    entry.present_in_grouped = grouped_it != grouped_by_symbol.end();
    entry.present_in_shared = shared_it != shared_by_symbol.end();
    if (entry.present_in_grouped && entry.present_in_shared) {
      const SymbolReplaySummary& g = *grouped_it->second;
      const SymbolReplaySummary& s = *shared_it->second;
      entry.event_log_checksum_match = g.event_log_checksum == s.event_log_checksum;
      entry.final_book_checksum_match = g.final_book_checksum == s.final_book_checksum;
      entry.execution_report_checksum_match =
          g.execution_report_checksum == s.execution_report_checksum;
      entry.diagnostics_checksum_match = g.diagnostics_checksum == s.diagnostics_checksum;
      entry.matched = entry.event_log_checksum_match && entry.final_book_checksum_match &&
                      entry.execution_report_checksum_match && entry.diagnostics_checksum_match;
    }
    if (!entry.matched) {
      ++report.mismatch_count;
    }
    report.symbols.push_back(entry);
  }

  report.matched = report.mismatch_count == 0 && report.combined_book_checksum_match &&
                   report.aggregate_checksum_match &&
                   report.symbol_count_grouped == report.symbol_count_shared;
  return report;
}

ReplayParityReport compare_replay_parity_file(const std::filesystem::path& path,
                                              EventLogFormat format,
                                              AggregateReplayConfig config) {
  EventLogReadResult read_result = read_event_log(path, format);
  if (!read_result.error.empty()) {
    return ReplayParityReport{};
  }
  return compare_replay_parity(read_result.events, config);
}

namespace {

// Replay one symbol's events through the grouped ReplayEngine and return its top-of-book
// L2 view. This mirrors replay_by_symbol's per-symbol config so the reconstructed book is
// the authoritative grouped final state for the symbol. Used only on a parity mismatch.
[[nodiscard]] L2View grouped_symbol_l2(std::span<const MarketDataEvent> events, SymbolId symbol_id,
                                       AggregateReplayConfig config, std::size_t depth) {
  std::vector<MarketDataEvent> symbol_events;
  for (const MarketDataEvent& event : events) {
    if (event.symbol_id == symbol_id) {
      symbol_events.push_back(event);
    }
  }
  ReplayConfig replay_config = config.replay_config;
  if (!config.validate_per_symbol_sequences) {
    replay_config.validate_sequence_numbers = false;
  }
  ReplayEngine replay(symbol_id, replay_config);
  (void)replay.replay_events(symbol_events);
  return replay.book().l2_view(depth);
}

void append_l2_side(std::ostringstream& out, std::string_view label,
                    const std::vector<L2Level>& levels) {
  out << "      " << label << ":";
  if (levels.empty()) {
    out << " (empty)";
  }
  for (const L2Level& level : levels) {
    out << " [" << level.price_ticks << "x" << level.quantity << "]";
  }
  out << "\n";
}

void append_first_differing_diagnostic(std::ostringstream& out,
                                       const std::vector<ReplayDiagnostic>& grouped,
                                       const std::vector<ReplayDiagnostic>& shared) {
  const std::size_t count = std::max(grouped.size(), shared.size());
  for (std::size_t i = 0; i < count; ++i) {
    const bool has_grouped = i < grouped.size();
    const bool has_shared = i < shared.size();
    const bool same = has_grouped && has_shared &&
                      grouped[i].event_index == shared[i].event_index &&
                      grouped[i].sequence_number == shared[i].sequence_number &&
                      grouped[i].severity == shared[i].severity &&
                      grouped[i].reason == shared[i].reason;
    if (same) {
      continue;
    }
    out << "      first differing diagnostic at index " << i << ":\n";
    if (has_grouped) {
      out << "        grouped: event_index=" << grouped[i].event_index
          << " seq=" << grouped[i].sequence_number << " severity="
          << to_string(grouped[i].severity) << " reason=\"" << grouped[i].reason << "\"\n";
    } else {
      out << "        grouped: (none)\n";
    }
    if (has_shared) {
      out << "        shared:  event_index=" << shared[i].event_index
          << " seq=" << shared[i].sequence_number << " severity="
          << to_string(shared[i].severity) << " reason=\"" << shared[i].reason << "\"\n";
    } else {
      out << "        shared:  (none)\n";
    }
    return;
  }
}

void append_symbol_field_diffs(std::ostringstream& out, const SymbolReplaySummary& g,
                               const SymbolReplaySummary& s) {
  const auto field = [&out](std::string_view name, std::uint64_t lhs, std::uint64_t rhs) {
    if (lhs != rhs) {
      out << "      " << name << ": grouped=" << lhs << " shared=" << rhs << "\n";
    }
  };
  field("event_count", static_cast<std::uint64_t>(g.event_count),
        static_cast<std::uint64_t>(s.event_count));
  field("event_log_checksum", g.event_log_checksum, s.event_log_checksum);
  field("final_book_checksum", g.final_book_checksum, s.final_book_checksum);
  field("execution_report_checksum", g.execution_report_checksum, s.execution_report_checksum);
  field("diagnostics_checksum", g.diagnostics_checksum, s.diagnostics_checksum);
  field("diagnostic_error_count", static_cast<std::uint64_t>(g.diagnostic_error_count),
        static_cast<std::uint64_t>(s.diagnostic_error_count));
  field("diagnostic_warning_count", static_cast<std::uint64_t>(g.diagnostic_warning_count),
        static_cast<std::uint64_t>(s.diagnostic_warning_count));
  if (g.error != s.error) {
    out << "      error: grouped=\"" << g.error << "\" shared=\"" << s.error << "\"\n";
  }
}

} // namespace

std::string describe_replay_parity(std::span<const MarketDataEvent> events,
                                   AggregateReplayConfig config) {
  const AggregateReplaySummary grouped = replay_by_symbol(events, config);
  const AggregateReplaySummary shared = replay_shared_by_symbol(events, config);

  std::map<SymbolId, const SymbolReplaySummary*> grouped_by_symbol;
  std::map<SymbolId, const SymbolReplaySummary*> shared_by_symbol;
  for (const SymbolReplaySummary& summary : grouped.symbols) {
    grouped_by_symbol[summary.symbol_id] = &summary;
  }
  for (const SymbolReplaySummary& summary : shared.symbols) {
    shared_by_symbol[summary.symbol_id] = &summary;
  }

  const bool combined_match = grouped.combined_book_checksum == shared.combined_book_checksum;
  const bool aggregate_match = grouped.aggregate_checksum == shared.aggregate_checksum;

  std::ostringstream out;
  out << "grouped-vs-shared replay parity (events=" << events.size()
      << ", grouped_symbols=" << grouped.symbol_count
      << ", shared_symbols=" << shared.symbol_count << ")\n";

  bool mismatch = !combined_match || !aggregate_match ||
                  grouped.symbol_count != shared.symbol_count;
  if (!combined_match) {
    out << "  combined_book_checksum MISMATCH grouped=" << grouped.combined_book_checksum
        << " shared=" << shared.combined_book_checksum << "\n";
  }
  if (!aggregate_match) {
    out << "  aggregate_checksum MISMATCH grouped=" << grouped.aggregate_checksum
        << " shared=" << shared.aggregate_checksum << "\n";
  }

  std::map<SymbolId, bool> all_symbols;
  for (const auto& item : grouped_by_symbol) {
    all_symbols[item.first] = true;
  }
  for (const auto& item : shared_by_symbol) {
    all_symbols[item.first] = true;
  }

  for (const auto& item : all_symbols) {
    const SymbolId symbol_id = item.first;
    const auto grouped_it = grouped_by_symbol.find(symbol_id);
    const auto shared_it = shared_by_symbol.find(symbol_id);
    const bool in_grouped = grouped_it != grouped_by_symbol.end();
    const bool in_shared = shared_it != shared_by_symbol.end();
    if (!in_grouped || !in_shared) {
      mismatch = true;
      out << "  symbol " << symbol_id << " present_in_grouped=" << in_grouped
          << " present_in_shared=" << in_shared << "\n";
      continue;
    }
    const SymbolReplaySummary& g = *grouped_it->second;
    const SymbolReplaySummary& s = *shared_it->second;
    const bool symbol_match = g.event_log_checksum == s.event_log_checksum &&
                              g.final_book_checksum == s.final_book_checksum &&
                              g.execution_report_checksum == s.execution_report_checksum &&
                              g.diagnostics_checksum == s.diagnostics_checksum;
    if (symbol_match) {
      continue;
    }
    mismatch = true;
    out << "  symbol " << symbol_id << " MISMATCH:\n";
    append_symbol_field_diffs(out, g, s);
    if (g.diagnostics_checksum != s.diagnostics_checksum) {
      append_first_differing_diagnostic(out, g.diagnostics, s.diagnostics);
    }
    if (g.final_book_checksum != s.final_book_checksum) {
      const L2View view = grouped_symbol_l2(events, symbol_id, config, 5);
      out << "      grouped expected final L2 (top 5):\n";
      append_l2_side(out, "bids", view.bids);
      append_l2_side(out, "asks", view.asks);
    }
  }

  if (!mismatch) {
    return "replay parity matched";
  }
  return out.str();
}

} // namespace asterion
