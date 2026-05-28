#pragma once

#include "asterion/inference/model.hpp"

#include <vector>

namespace asterion {

class LinearModel final : public Model {
public:
  LinearModel(std::vector<double> weights, double bias);
  [[nodiscard]] std::string_view backend_name() const noexcept override;
  [[nodiscard]] double score(std::span<const double> features) const override;

private:
  std::vector<double> weights_;
  double bias_{0.0};
};

} // namespace asterion
