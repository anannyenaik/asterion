#pragma once

#include <span>

namespace asterion {

class Model {
public:
  virtual ~Model() = default;
  [[nodiscard]] virtual double score(std::span<const double> features) const = 0;
};

} // namespace asterion
