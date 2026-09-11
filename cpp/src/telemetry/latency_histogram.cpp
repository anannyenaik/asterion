#include "asterion/telemetry/latency_histogram.hpp"

#include <algorithm>
#include <cmath>

namespace asterion {

void LatencyHistogram::record(std::uint64_t duration_ns) { samples_.push_back(duration_ns); }

void LatencyHistogram::clear() noexcept { samples_.clear(); }

LatencySummary LatencyHistogram::summary() const {
  LatencySummary result;
  result.count = samples_.size();
  if (samples_.empty()) {
    return result;
  }

  std::vector<std::uint64_t> sorted = samples_;
  std::sort(sorted.begin(), sorted.end());
  result.min_ns = sorted.front();
  result.max_ns = sorted.back();

  const auto percentile = [&](double p) {
    const double rank = std::ceil(p * static_cast<double>(sorted.size()));
    const auto index = static_cast<std::size_t>(rank <= 1.0 ? 0.0 : rank - 1.0);
    return sorted[std::min(index, sorted.size() - 1U)];
  };

  result.p50_ns = percentile(0.50);
  result.p90_ns = percentile(0.90);
  result.p99_ns = percentile(0.99);
  result.p999_ns = percentile(0.999);
  return result;
}

} // namespace asterion
