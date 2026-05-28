#pragma once

#include "asterion/strategy/strategy.hpp"

namespace asterion {

class MarketMaker final : public Strategy {
public:
  explicit MarketMaker(Quantity quote_quantity = 10);
  [[nodiscard]] std::vector<StrategyDecision> on_l2_update(const L2View& view) override;

private:
  Quantity quote_quantity_;
};

} // namespace asterion
