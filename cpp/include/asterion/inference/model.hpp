#pragma once

#include <string_view>
#include <span>

namespace asterion {

class Model {
public:
  virtual ~Model() = default;
  [[nodiscard]] virtual std::string_view backend_name() const noexcept { return "model"; }
  [[nodiscard]] virtual std::string_view model_name() const noexcept { return "model"; }
  [[nodiscard]] virtual std::string_view input_shape() const noexcept { return "n/a"; }
  [[nodiscard]] virtual std::string_view output_shape() const noexcept { return "n/a"; }
  [[nodiscard]] virtual double score(std::span<const double> features) const = 0;
};

} // namespace asterion
