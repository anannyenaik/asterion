#include "asterion/market_data/replay_aggregate.hpp"

#include "asterion/core/checksum.hpp"

#include <map>
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
  for (const SymbolReplaySummary& symbol_summary : summary.symbols) {
    seed = append_summary_checksum(seed, symbol_summary);
  }
  seed = checksum_append_string(seed, summary.error);
  return seed;
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

} // namespace asterion
