#include "asterion/strategy/imbalance_strategy.hpp"

namespace asterion {

ImbalanceStrategy::ImbalanceStrategy(double threshold, Quantity order_quantity)
    : threshold_(threshold), order_quantity_(order_quantity) {}

std::vector<StrategyDecision> ImbalanceStrategy::on_l2_update(const L2View& view) {
  if (view.bids.empty() || view.asks.empty() || order_quantity_ <= 0) {
    return {};
  }

  const double bid_quantity = static_cast<double>(view.bids.front().quantity);
  const double ask_quantity = static_cast<double>(view.asks.front().quantity);
  const double total = bid_quantity + ask_quantity;
  if (total <= 0.0) {
    return {};
  }

  const double bid_imbalance = bid_quantity / total;
  if (bid_imbalance >= threshold_) {
    return {StrategyDecision{Side::Buy, OrderType::Limit, view.bids.front().price_ticks,
                             order_quantity_}};
  }
  if ((1.0 - bid_imbalance) >= threshold_) {
    return {StrategyDecision{Side::Sell, OrderType::Limit, view.asks.front().price_ticks,
                             order_quantity_}};
  }
  return {};
}

} // namespace asterion
