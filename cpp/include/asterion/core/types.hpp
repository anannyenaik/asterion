#pragma once

#include <cstdint>
#include <string_view>

namespace asterion {

using TimestampNs = std::int64_t;
using PriceTicks = std::int64_t;
using Quantity = std::int64_t;
using SymbolId = std::uint32_t;
using OrderId = std::uint64_t;
using ClientOrderId = std::uint64_t;
using SequenceNumber = std::uint64_t;
using TradeId = std::uint64_t;

inline constexpr OrderId kInvalidOrderId = 0;
inline constexpr ClientOrderId kInvalidClientOrderId = 0;
inline constexpr SymbolId kInvalidSymbolId = 0;

enum class Side : std::uint8_t { None = 0, Buy = 1, Sell = 2 };

[[nodiscard]] constexpr bool is_valid_side(Side side) noexcept {
  return side == Side::Buy || side == Side::Sell;
}

[[nodiscard]] constexpr Side opposite(Side side) noexcept {
  if (side == Side::Buy) {
    return Side::Sell;
  }
  if (side == Side::Sell) {
    return Side::Buy;
  }
  return Side::None;
}

[[nodiscard]] constexpr std::string_view to_string(Side side) noexcept {
  switch (side) {
  case Side::Buy:
    return "Buy";
  case Side::Sell:
    return "Sell";
  case Side::None:
    return "None";
  }
  return "Unknown";
}

} // namespace asterion
