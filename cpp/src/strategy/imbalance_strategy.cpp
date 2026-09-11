#include "asterion/strategy/imbalance_strategy.hpp"

namespace asterion {

ImbalanceStrategy::ImbalanceStrategy(double threshold, Quantity order_quantity)
    : threshold_(threshold), order_quantity_(order_quantity) {}

StrategyDecisionBatch ImbalanceStrategy::on_l2_update_fixed(const L2View& view) const noexcept {
  StrategyDecisionBatch output;
  if (view.bids.empty() || view.asks.empty() || order_quantity_ <= 0) {
    return output;
  }

  const double bid_quantity = static_cast<double>(view.bids.front().quantity);
  const double ask_quantity = static_cast<double>(view.asks.front().quantity);
  const double total = bid_quantity + ask_quantity;
  if (total <= 0.0) {
    return output;
  }

  const double bid_imbalance = bid_quantity / total;
  if (bid_imbalance >= threshold_) {
    output.push_back(
        StrategyDecision{Side::Buy, OrderType::Limit, view.bids.front().price_ticks,
                         order_quantity_});
    return output;
  }
  if ((1.0 - bid_imbalance) >= threshold_) {
    output.push_back(
        StrategyDecision{Side::Sell, OrderType::Limit, view.asks.front().price_ticks,
                         order_quantity_});
    return output;
  }
  return output;
}

std::vector<StrategyDecision> ImbalanceStrategy::on_l2_update(const L2View& view) {
  const StrategyDecisionBatch fixed = on_l2_update_fixed(view);
  std::vector<StrategyDecision> output;
  output.reserve(fixed.size);
  output.insert(output.end(), fixed.begin(), fixed.end());
  return output;
}

} // namespace asterion
