#pragma once

#include "asterion/inference/model.hpp"

#include <string>
#include <vector>

namespace asterion {

class LinearModel final : public Model {
public:
  LinearModel(std::vector<double> weights, double bias);
  [[nodiscard]] std::string_view backend_name() const noexcept override;
  [[nodiscard]] std::string_view model_name() const noexcept override;
  [[nodiscard]] std::string_view input_shape() const noexcept override;
  [[nodiscard]] std::string_view output_shape() const noexcept override;
  [[nodiscard]] double score(std::span<const double> features) const override;

private:
  std::vector<double> weights_;
  double bias_{0.0};
  std::string model_name_{"linear_model"};
  std::string input_shape_;
  std::string output_shape_{"1x1"};
};

} // namespace asterion
