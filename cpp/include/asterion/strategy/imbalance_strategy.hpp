#pragma once

#include "asterion/strategy/strategy.hpp"

namespace asterion {

class ImbalanceStrategy final : public Strategy {
public:
  explicit ImbalanceStrategy(double threshold = 0.65, Quantity order_quantity = 10);
  [[nodiscard]] std::vector<StrategyDecision> on_l2_update(const L2View& view) override;

private:
  double threshold_;
  Quantity order_quantity_;
};

} // namespace asterion
