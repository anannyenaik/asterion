#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace asterion {

struct LatencySummary {
  std::size_t count{0};
  std::uint64_t min_ns{0};
  std::uint64_t max_ns{0};
  std::uint64_t p50_ns{0};
  std::uint64_t p90_ns{0};
  std::uint64_t p99_ns{0};
  std::uint64_t p999_ns{0};
};

class LatencyHistogram {
public:
  void record(std::uint64_t duration_ns);
  void clear() noexcept;
  [[nodiscard]] std::size_t count() const noexcept { return samples_.size(); }
  [[nodiscard]] LatencySummary summary() const;

private:
  std::vector<std::uint64_t> samples_;
};

} // namespace asterion
