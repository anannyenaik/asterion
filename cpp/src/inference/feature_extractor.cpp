#include "asterion/inference/feature_extractor.hpp"

namespace asterion {

std::vector<double> FeatureExtractor::extract(const L2View& view) const {
  if (view.bids.empty() || view.asks.empty()) {
    return {0.0, 0.0, 0.0, 0.0};
  }

  const double bid = static_cast<double>(view.bids.front().price_ticks);
  const double ask = static_cast<double>(view.asks.front().price_ticks);
  const double bid_qty = static_cast<double>(view.bids.front().quantity);
  const double ask_qty = static_cast<double>(view.asks.front().quantity);
  const double total_qty = bid_qty + ask_qty;
  const double imbalance = total_qty == 0.0 ? 0.0 : (bid_qty - ask_qty) / total_qty;
  return {ask - bid, (ask + bid) / 2.0, imbalance, total_qty};
}

} // namespace asterion
