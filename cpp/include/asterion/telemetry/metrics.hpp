#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace asterion {

class MetricsRegistry {
public:
  void increment(std::string name, std::int64_t delta = 1);
  [[nodiscard]] std::int64_t value(const std::string& name) const;
  [[nodiscard]] const std::unordered_map<std::string, std::int64_t>& counters() const noexcept {
    return counters_;
  }

private:
  std::unordered_map<std::string, std::int64_t> counters_;
};

} // namespace asterion
