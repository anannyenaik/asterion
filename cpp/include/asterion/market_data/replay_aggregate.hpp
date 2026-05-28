#pragma once

#include "asterion/market_data/event.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace asterion {

struct AggregateReplayConfig {
  ReplayConfig replay_config;
  bool validate_per_symbol_sequences{false};
};

struct SymbolReplaySummary {
  SymbolId symbol_id{kInvalidSymbolId};
  std::size_t event_count{0};
  SequenceNumber first_sequence{0};
  SequenceNumber last_sequence{0};
  TimestampNs first_timestamp_ns{0};
  TimestampNs last_timestamp_ns{0};
  bool sequence_valid{true};
  std::uint64_t event_log_checksum{0};
  std::uint64_t final_book_checksum{0};
  std::uint64_t execution_report_checksum{0};
  std::uint64_t diagnostics_checksum{0};
  std::size_t diagnostic_count{0};
  std::size_t diagnostic_error_count{0};
  std::size_t diagnostic_warning_count{0};
  std::vector<ReplayDiagnostic> diagnostics;
  std::string error;
};

struct AggregateReplaySummary {
  std::size_t total_events{0};
  std::size_t symbol_count{0};
  std::uint64_t combined_book_checksum{0};
  std::uint64_t aggregate_checksum{0};
  std::vector<SymbolReplaySummary> symbols;
  std::string error;
};

[[nodiscard]] AggregateReplaySummary replay_by_symbol(
    std::span<const MarketDataEvent> events, AggregateReplayConfig config = {});
[[nodiscard]] AggregateReplaySummary replay_file_by_symbol(
    const std::filesystem::path& path, EventLogFormat format = EventLogFormat::Auto,
    AggregateReplayConfig config = {});
[[nodiscard]] AggregateReplaySummary replay_shared_by_symbol(
    std::span<const MarketDataEvent> events, AggregateReplayConfig config = {});
[[nodiscard]] AggregateReplaySummary replay_file_shared_by_symbol(
    const std::filesystem::path& path, EventLogFormat format = EventLogFormat::Auto,
    AggregateReplayConfig config = {});

} // namespace asterion
