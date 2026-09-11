#include "asterion/telemetry/metrics.hpp"

namespace asterion {

void MetricsRegistry::increment(std::string name, std::int64_t delta) {
  counters_[std::move(name)] += delta;
}

std::int64_t MetricsRegistry::value(const std::string& name) const {
  const auto it = counters_.find(name);
  return it == counters_.end() ? 0 : it->second;
}

} // namespace asterion
