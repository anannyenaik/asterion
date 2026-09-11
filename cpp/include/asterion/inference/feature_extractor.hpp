#pragma once

#include "asterion/book/order_book.hpp"
#include "asterion/book/l2_view.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace asterion {

inline constexpr std::uint32_t kL2FeatureVersion = 1;
inline constexpr std::size_t kL2FeatureCount = 4;
inline constexpr std::array<std::string_view, kL2FeatureCount> kL2FeatureNames{
    "spread_ticks", "mid_price_ticks", "top_level_imbalance", "top_level_quantity"};

enum class FeatureExtractionStatus : std::uint8_t { Ok = 0, InsufficientCapacity = 1 };

[[nodiscard]] std::string_view to_string(FeatureExtractionStatus status) noexcept;

struct FeatureBuffer {
  std::span<double> values;
  std::size_t size{0};

  [[nodiscard]] std::size_t capacity() const noexcept { return values.size(); }
  [[nodiscard]] std::span<double> used() noexcept { return values.first(size); }
  [[nodiscard]] std::span<const double> used() const noexcept { return values.first(size); }
};

struct FeatureVector {
  std::uint32_t version{kL2FeatureVersion};
  std::vector<std::string> names;
  std::vector<double> values;
};

class FeatureExtractor {
public:
  [[nodiscard]] std::uint32_t feature_version() const noexcept;
  [[nodiscard]] std::size_t feature_count() const noexcept;
  [[nodiscard]] std::span<const std::string_view> feature_name_views() const noexcept;
  [[nodiscard]] std::vector<std::string> feature_names() const;
  [[nodiscard]] FeatureExtractionStatus extract_into(const L2View& view,
                                                     FeatureBuffer& out) const noexcept;
  [[nodiscard]] std::vector<double> extract(const L2View& view) const;
  [[nodiscard]] FeatureVector extract_versioned(const L2View& view) const;
  [[nodiscard]] std::vector<double> extract_from_book(const OrderBook& book,
                                                      std::size_t depth = 1) const;
  [[nodiscard]] FeatureVector extract_versioned_from_book(const OrderBook& book,
                                                          std::size_t depth = 1) const;

  template <typename Book>
  [[nodiscard]] FeatureExtractionStatus extract_from_book_into(const Book& book,
                                                               FeatureBuffer& out,
                                                               L2View& scratch_view,
                                                               std::size_t depth = 1) const {
    book.fill_l2_view(depth, scratch_view);
    return extract_into(scratch_view, out);
  }
};

[[nodiscard]] FeatureExtractionStatus extract_features_into(const L2View& view,
                                                            FeatureBuffer& out) noexcept;

} // namespace asterion
