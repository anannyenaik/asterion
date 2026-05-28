#pragma once

#include "asterion/core/types.hpp"

namespace asterion {

struct RiskLimits {
  Quantity max_order_quantity{1'000};
  std::int64_t max_notional_ticks{1'000'000};
  Quantity max_position_per_symbol{10'000};
  std::int64_t max_gross_exposure_ticks{10'000'000};
  PriceTicks price_band_ticks{100};
  TimestampNs stale_after_ns{1'000'000'000};
  // Phase 6 controls. Each defaults to a disabled sentinel so that the original
  // six-field aggregate initialisers keep their previous behaviour unchanged.
  // Maximum total resting (working) limit-order quantity per symbol; 0 disables.
  Quantity max_open_order_quantity{0};
  // Maximum messages per client within rate_window_ns; 0 disables rate limiting.
  std::uint32_t max_messages_per_window{0};
  TimestampNs rate_window_ns{0};
  // Reject a new order that would cross the same client's resting opposite side.
  bool enable_self_trade_prevention{false};
};

} // namespace asterion
