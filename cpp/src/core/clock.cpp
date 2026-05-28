#include "asterion/core/clock.hpp"

#include <chrono>

namespace asterion {

TimestampNs monotonic_now_ns() noexcept {
  const auto now = std::chrono::steady_clock::now().time_since_epoch();
  return std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
}

} // namespace asterion
