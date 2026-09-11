#include "asterion/strategy/market_maker.hpp"

namespace asterion {

MarketMaker::MarketMaker(Quantity quote_quantity) : quote_quantity_(quote_quantity) {}

std::vector<StrategyDecision> MarketMaker::on_l2_update(const L2View& view) {
  if (view.bids.empty() || view.asks.empty() || quote_quantity_ <= 0) {
    return {};
  }

  const PriceTicks best_bid = view.bids.front().price_ticks;
  const PriceTicks best_ask = view.asks.front().price_ticks;
  if (best_ask <= best_bid + 1) {
    return {};
  }

  return {StrategyDecision{Side::Buy, OrderType::Limit, best_bid + 1, quote_quantity_},
          StrategyDecision{Side::Sell, OrderType::Limit, best_ask - 1, quote_quantity_}};
}

} // namespace asterion
