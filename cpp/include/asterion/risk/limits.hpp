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
};

} // namespace asterion
