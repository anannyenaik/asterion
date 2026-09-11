#pragma once

#include "asterion/core/types.hpp"

#include <cstdint>
#include <string_view>

namespace asterion {

enum class RateLimitMode : std::uint8_t { FixedWindow = 1, SlidingWindow = 2 };
enum class DisconnectOrderPolicy : std::uint8_t { RejectNewOrders = 1, AllowNewOrders = 2 };

[[nodiscard]] constexpr std::string_view to_string(RateLimitMode mode) noexcept {
  switch (mode) {
  case RateLimitMode::FixedWindow:
    return "fixed-window";
  case RateLimitMode::SlidingWindow:
    return "sliding-window";
  }
  return "unknown";
}

[[nodiscard]] constexpr std::string_view to_string(DisconnectOrderPolicy policy) noexcept {
  switch (policy) {
  case DisconnectOrderPolicy::RejectNewOrders:
    return "reject-new-orders";
  case DisconnectOrderPolicy::AllowNewOrders:
    return "allow-new-orders";
  }
  return "unknown";
}

struct RiskLimits {
  Quantity max_order_quantity{1'000};
  std::int64_t max_notional_ticks{1'000'000};
  Quantity max_position_per_symbol{10'000};
  std::int64_t max_gross_exposure_ticks{10'000'000};
  PriceTicks price_band_ticks{100};
  TimestampNs stale_after_ns{1'000'000'000};
  // Optional controls. Each defaults to a disabled sentinel so that the original
  // six-field aggregate initialisers keep their previous behaviour unchanged.
  // Maximum total resting (working) limit-order quantity per symbol; 0 disables.
  Quantity max_open_order_quantity{0};
  // Maximum messages per client within rate_window_ns; 0 disables rate limiting.
  std::uint32_t max_messages_per_window{0};
  TimestampNs rate_window_ns{0};
  // Reject a new order that would cross the same client's resting opposite side.
  bool enable_self_trade_prevention{false};
  // Fixed-window remains the default for compatibility; sliding-window is opt-in.
  RateLimitMode rate_limit_mode{RateLimitMode::FixedWindow};
  // Simulated disconnect handling is opt-in for cancellation. New orders are
  // rejected while disconnected unless a test explicitly chooses AllowNewOrders.
  bool cancel_on_disconnect{false};
  DisconnectOrderPolicy disconnect_order_policy{DisconnectOrderPolicy::RejectNewOrders};
};

} // namespace asterion
