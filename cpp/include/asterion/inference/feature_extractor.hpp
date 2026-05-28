#pragma once

#include "asterion/book/order_book.hpp"
#include "asterion/book/l2_view.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace asterion {

inline constexpr std::uint32_t kL2FeatureVersion = 1;

struct FeatureVector {
  std::uint32_t version{kL2FeatureVersion};
  std::vector<std::string> names;
  std::vector<double> values;
};

class FeatureExtractor {
public:
  [[nodiscard]] std::uint32_t feature_version() const noexcept;
  [[nodiscard]] std::vector<std::string> feature_names() const;
  [[nodiscard]] std::vector<double> extract(const L2View& view) const;
  [[nodiscard]] FeatureVector extract_versioned(const L2View& view) const;
  [[nodiscard]] std::vector<double> extract_from_book(const OrderBook& book,
                                                      std::size_t depth = 1) const;
  [[nodiscard]] FeatureVector extract_versioned_from_book(const OrderBook& book,
                                                          std::size_t depth = 1) const;
};

} // namespace asterion
