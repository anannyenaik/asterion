#pragma once

#include "asterion/book/order_book.hpp"
#include "asterion/market_data/event.hpp"

#include <filesystem>
#include <span>
#include <string>

namespace asterion {

struct ReplayConfig {
  bool validate_sequence_numbers{true};
  bool validate_timestamps{true};
  bool max_speed{true};
};

struct ReplayResult {
  std::size_t events_processed{0};
  bool sequence_valid{true};
  std::uint64_t final_book_checksum{0};
  std::uint64_t execution_report_checksum{0};
  std::string error;
};

class ReplayEngine {
public:
  explicit ReplayEngine(SymbolId symbol_id, ReplayConfig config = {});

  [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
  [[nodiscard]] ReplayResult replay_file(const std::filesystem::path& path);
  [[nodiscard]] ReplayResult replay_events(std::span<const MarketDataEvent> events);

private:
  [[nodiscard]] bool apply_event(const MarketDataEvent& event, ReplayResult& result);
  void update_activity_checksum(const MarketDataEvent& event, ReplayResult& result) const;

  ReplayConfig config_;
  OrderBook book_;
};

} // namespace asterion
