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

// Per-symbol parity between the grouped and shared replay paths. Each flag is true
// when the two paths agree on that checksum for the symbol.
struct SymbolParityEntry {
  SymbolId symbol_id{kInvalidSymbolId};
  bool present_in_grouped{false};
  bool present_in_shared{false};
  bool event_log_checksum_match{false};
  bool final_book_checksum_match{false};
  bool execution_report_checksum_match{false};
  bool diagnostics_checksum_match{false};
  bool matched{false};
};

// Structured grouped-vs-shared replay parity report. The shared multi-symbol path
// stays opt-in; this report is how parity is demonstrated. matched is true only when
// every per-symbol checksum, the combined book checksum and the aggregate checksum
// agree and both paths see the same symbols.
struct ReplayParityReport {
  bool matched{false};
  std::size_t symbol_count_grouped{0};
  std::size_t symbol_count_shared{0};
  std::size_t mismatch_count{0};
  bool combined_book_checksum_match{false};
  bool aggregate_checksum_match{false};
  std::uint64_t grouped_combined_book_checksum{0};
  std::uint64_t shared_combined_book_checksum{0};
  std::uint64_t grouped_aggregate_checksum{0};
  std::uint64_t shared_aggregate_checksum{0};
  std::vector<SymbolParityEntry> symbols;
};

[[nodiscard]] ReplayParityReport compare_replay_parity(std::span<const MarketDataEvent> events,
                                                       AggregateReplayConfig config = {});
[[nodiscard]] ReplayParityReport compare_replay_parity_file(
    const std::filesystem::path& path, EventLogFormat format = EventLogFormat::Auto,
    AggregateReplayConfig config = {});

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
