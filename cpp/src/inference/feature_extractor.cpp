#include "asterion/inference/feature_extractor.hpp"

namespace asterion {

std::string_view to_string(FeatureExtractionStatus status) noexcept {
  switch (status) {
  case FeatureExtractionStatus::Ok:
    return "ok";
  case FeatureExtractionStatus::InsufficientCapacity:
    return "insufficient_capacity";
  }
  return "unknown";
}

std::uint32_t FeatureExtractor::feature_version() const noexcept { return kL2FeatureVersion; }

std::size_t FeatureExtractor::feature_count() const noexcept { return kL2FeatureCount; }

std::span<const std::string_view> FeatureExtractor::feature_name_views() const noexcept {
  return kL2FeatureNames;
}

std::vector<std::string> FeatureExtractor::feature_names() const {
  std::vector<std::string> names;
  names.reserve(kL2FeatureNames.size());
  for (const std::string_view name : kL2FeatureNames) {
    names.emplace_back(name);
  }
  return names;
}

FeatureExtractionStatus FeatureExtractor::extract_into(const L2View& view,
                                                       FeatureBuffer& out) const noexcept {
  if (out.capacity() < kL2FeatureCount) {
    out.size = 0;
    return FeatureExtractionStatus::InsufficientCapacity;
  }

  double spread_ticks = 0.0;
  double mid_price_ticks = 0.0;
  double top_level_imbalance = 0.0;
  double top_level_quantity = 0.0;

  if (view.bids.empty() || view.asks.empty()) {
    out.values[0] = spread_ticks;
    out.values[1] = mid_price_ticks;
    out.values[2] = top_level_imbalance;
    out.values[3] = top_level_quantity;
    out.size = kL2FeatureCount;
    return FeatureExtractionStatus::Ok;
  }

  const double bid = static_cast<double>(view.bids.front().price_ticks);
  const double ask = static_cast<double>(view.asks.front().price_ticks);
  const double bid_qty = static_cast<double>(view.bids.front().quantity);
  const double ask_qty = static_cast<double>(view.asks.front().quantity);
  top_level_quantity = bid_qty + ask_qty;
  spread_ticks = ask - bid;
  mid_price_ticks = (ask + bid) / 2.0;
  top_level_imbalance =
      top_level_quantity == 0.0 ? 0.0 : (bid_qty - ask_qty) / top_level_quantity;

  out.values[0] = spread_ticks;
  out.values[1] = mid_price_ticks;
  out.values[2] = top_level_imbalance;
  out.values[3] = top_level_quantity;
  out.size = kL2FeatureCount;
  return FeatureExtractionStatus::Ok;
}

std::vector<double> FeatureExtractor::extract(const L2View& view) const {
  std::vector<double> values(kL2FeatureCount);
  FeatureBuffer buffer{values};
  const FeatureExtractionStatus status = extract_into(view, buffer);
  if (status == FeatureExtractionStatus::Ok) {
    values.resize(buffer.size);
  } else {
    values.clear();
  }
  return values;
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

FeatureExtractionStatus extract_features_into(const L2View& view, FeatureBuffer& out) noexcept {
  return FeatureExtractor{}.extract_into(view, out);
}

} // namespace asterion
