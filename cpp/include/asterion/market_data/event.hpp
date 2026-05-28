#pragma once

#include "asterion/core/types.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace asterion {

enum class MarketEventType : std::uint8_t {
  Add = 1,
  Cancel = 2,
  Replace = 3,
  Execute = 4,
  Trade = 5,
  Snapshot = 6,
  Heartbeat = 7
};

struct MarketDataEvent {
  TimestampNs timestamp_ns{0};
  SequenceNumber sequence_number{0};
  SymbolId symbol_id{kInvalidSymbolId};
  MarketEventType event_type{MarketEventType::Heartbeat};
  Side side{Side::None};
  PriceTicks price_ticks{0};
  Quantity quantity{0};
  OrderId order_id{kInvalidOrderId};
  TradeId trade_id{0};
  std::uint32_t flags{0};
};

[[nodiscard]] std::string_view to_string(MarketEventType type) noexcept;
[[nodiscard]] std::optional<MarketEventType> parse_market_event_type(std::string_view value);
[[nodiscard]] std::optional<Side> parse_side(std::string_view value);
[[nodiscard]] std::optional<MarketDataEvent> parse_market_data_event_csv(std::string_view line,
                                                                          std::string* error);
[[nodiscard]] std::string market_data_event_to_csv(const MarketDataEvent& event);
[[nodiscard]] std::uint64_t append_to_checksum(std::uint64_t seed,
                                               const MarketDataEvent& event) noexcept;

} // namespace asterion
