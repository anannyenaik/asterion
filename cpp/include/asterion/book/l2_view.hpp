#pragma once

#include "asterion/core/types.hpp"

#include <cstddef>
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

  void clear() noexcept {
    symbol_id = kInvalidSymbolId;
    bids.clear();
    asks.clear();
  }

  void reserve(std::size_t depth) {
    bids.reserve(depth);
    asks.reserve(depth);
  }
};

} // namespace asterion
