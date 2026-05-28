#pragma once

#include <cstdint>

namespace asterion {

struct AllocationSnapshot {
  std::uint64_t allocations{0};
  std::uint64_t deallocations{0};
  std::uint64_t bytes_allocated{0};
};

void reset_allocation_counters() noexcept;
[[nodiscard]] AllocationSnapshot allocation_snapshot() noexcept;
void set_allocation_tracking_enabled(bool enabled) noexcept;
[[nodiscard]] bool allocation_tracking_enabled() noexcept;

class ScopedAllocationCounter {
public:
  explicit ScopedAllocationCounter(bool reset = true) noexcept;
  ScopedAllocationCounter(const ScopedAllocationCounter&) = delete;
  ScopedAllocationCounter& operator=(const ScopedAllocationCounter&) = delete;
  ~ScopedAllocationCounter() noexcept;

  [[nodiscard]] AllocationSnapshot snapshot() const noexcept;

private:
  bool previous_enabled_{true};
};

} // namespace asterion
