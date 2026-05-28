#pragma once

#include "asterion/core/types.hpp"

#include <vector>

namespace asterion {

struct L2Level {
  PriceTicks price_ticks{0};
  Quantity quantity{0};
};

struct L2View {
  SymbolId symbol_id{kInvalidSymbolId};
  std::vector<L2Level> bids;
  std::vector<L2Level> asks;
};

} // namespace asterion
