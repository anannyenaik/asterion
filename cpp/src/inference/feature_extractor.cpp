#include "asterion/inference/feature_extractor.hpp"

namespace asterion {

std::uint32_t FeatureExtractor::feature_version() const noexcept { return kL2FeatureVersion; }

std::vector<std::string> FeatureExtractor::feature_names() const {
  return {"spread_ticks", "mid_price_ticks", "top_level_imbalance", "top_level_quantity"};
}

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

FeatureVector FeatureExtractor::extract_versioned(const L2View& view) const {
  return FeatureVector{feature_version(), feature_names(), extract(view)};
}

std::vector<double> FeatureExtractor::extract_from_book(const OrderBook& book,
                                                        std::size_t depth) const {
  return extract(book.l2_view(depth));
}

FeatureVector FeatureExtractor::extract_versioned_from_book(const OrderBook& book,
                                                            std::size_t depth) const {
  return extract_versioned(book.l2_view(depth));
}

} // namespace asterion
