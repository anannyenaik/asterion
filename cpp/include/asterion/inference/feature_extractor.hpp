#pragma once

#include "asterion/book/l2_view.hpp"

#include <vector>

namespace asterion {

class FeatureExtractor {
public:
  [[nodiscard]] std::vector<double> extract(const L2View& view) const;
};

} // namespace asterion
