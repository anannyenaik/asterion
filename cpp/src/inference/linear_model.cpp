#include "asterion/inference/linear_model.hpp"

#include <algorithm>

namespace asterion {

LinearModel::LinearModel(std::vector<double> weights, double bias)
    : weights_(std::move(weights)), bias_(bias) {}

double LinearModel::score(std::span<const double> features) const {
  const std::size_t count = std::min(weights_.size(), features.size());
  double output = bias_;
  for (std::size_t i = 0; i < count; ++i) {
    output += weights_[i] * features[i];
  }
  return output;
}

} // namespace asterion
