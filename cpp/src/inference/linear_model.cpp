#include "asterion/inference/linear_model.hpp"

#include <algorithm>

namespace asterion {

LinearModel::LinearModel(std::vector<double> weights, double bias)
    : weights_(std::move(weights)), bias_(bias),
      input_shape_("1x" + std::to_string(weights_.size())) {}

std::string_view LinearModel::backend_name() const noexcept { return "linear"; }

std::string_view LinearModel::model_name() const noexcept { return model_name_; }

std::string_view LinearModel::input_shape() const noexcept { return input_shape_; }

std::string_view LinearModel::output_shape() const noexcept { return output_shape_; }

double LinearModel::score(std::span<const double> features) const {
  const std::size_t count = std::min(weights_.size(), features.size());
  double output = bias_;
  for (std::size_t i = 0; i < count; ++i) {
    output += weights_[i] * features[i];
  }
  return output;
}

} // namespace asterion
