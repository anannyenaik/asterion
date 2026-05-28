#pragma once

#include "asterion/book/l2_view.hpp"
#include "asterion/matching/matching_engine.hpp"

#include <vector>

namespace asterion {

struct StrategyDecision {
  Side side{Side::None};
  OrderType order_type{OrderType::Limit};
  PriceTicks price_ticks{0};
  Quantity quantity{0};
};

class Strategy {
public:
  virtual ~Strategy() = default;
  [[nodiscard]] virtual std::vector<StrategyDecision> on_l2_update(const L2View& view) = 0;
};

} // namespace asterion
