#pragma once

#include "asterion/book/order_book.hpp"
#include "asterion/market_data/event.hpp"
#include "asterion/market_data/event_log.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace asterion {

enum class ReplayDiagnosticSeverity : std::uint8_t { Info = 1, Warning = 2, Error = 3 };

struct ReplayDiagnostic {
  std::size_t event_index{0};
  SequenceNumber sequence_number{0};
  SymbolId symbol_id{kInvalidSymbolId};
  ReplayDiagnosticSeverity severity{ReplayDiagnosticSeverity::Info};
  std::string reason;
};

struct ReplayConfig {
  bool validate_sequence_numbers{true};
  bool validate_timestamps{true};
  bool validate_book_state{true};
  bool max_speed{true};
};

struct ReplayResult {
  std::size_t events_processed{0};
  bool sequence_valid{true};
  std::uint64_t event_log_checksum{0};
  std::uint64_t final_book_checksum{0};
  std::uint64_t execution_report_checksum{0};
  std::uint64_t diagnostics_checksum{0};
  std::size_t diagnostic_error_count{0};
  std::size_t diagnostic_warning_count{0};
  std::vector<ReplayDiagnostic> diagnostics;
  std::string error;
};

[[nodiscard]] std::string_view to_string(ReplayDiagnosticSeverity severity) noexcept;
[[nodiscard]] std::uint64_t append_to_checksum(std::uint64_t seed,
                                               const ReplayDiagnostic& diagnostic) noexcept;

class ReplayEngine {
public:
  explicit ReplayEngine(SymbolId symbol_id, ReplayConfig config = {});

  [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
  [[nodiscard]] ReplayResult replay_file(const std::filesystem::path& path,
                                         EventLogFormat format = EventLogFormat::Auto);
  [[nodiscard]] ReplayResult replay_events(std::span<const MarketDataEvent> events);

private:
  [[nodiscard]] bool apply_event(const MarketDataEvent& event, std::size_t event_index,
                                 ReplayResult& result);
  [[nodiscard]] bool validate_event_before_apply(const MarketDataEvent& event,
                                                 std::size_t event_index, ReplayResult& result);
  [[nodiscard]] bool validate_book_after_apply(const MarketDataEvent& event,
                                               std::size_t event_index, ReplayResult& result);
  void add_diagnostic(ReplayResult& result, std::size_t event_index,
                      const MarketDataEvent& event, ReplayDiagnosticSeverity severity,
                      std::string reason) const;
  void update_activity_checksum(const MarketDataEvent& event, ReplayResult& result) const;

  ReplayConfig config_;
  OrderBook book_;
};

} // namespace asterion
