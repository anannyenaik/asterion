#include "asterion/core/allocation_tracker.hpp"

#include <atomic>
#include <cstdlib>
#include <new>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

namespace asterion {
namespace {

std::atomic_bool g_tracking_enabled{true};
std::atomic<std::uint64_t> g_allocations{0};
std::atomic<std::uint64_t> g_deallocations{0};
std::atomic<std::uint64_t> g_bytes_allocated{0};

} // namespace

namespace allocation_tracker_detail {

void record_allocation(std::size_t size) noexcept {
  if (!g_tracking_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  g_allocations.fetch_add(1, std::memory_order_relaxed);
  g_bytes_allocated.fetch_add(static_cast<std::uint64_t>(size), std::memory_order_relaxed);
}

void record_deallocation() noexcept {
  if (!g_tracking_enabled.load(std::memory_order_relaxed)) {
    return;
  }
  g_deallocations.fetch_add(1, std::memory_order_relaxed);
}

} // namespace allocation_tracker_detail

void reset_allocation_counters() noexcept {
  g_allocations.store(0, std::memory_order_relaxed);
  g_deallocations.store(0, std::memory_order_relaxed);
  g_bytes_allocated.store(0, std::memory_order_relaxed);
}

AllocationSnapshot allocation_snapshot() noexcept {
  return AllocationSnapshot{g_allocations.load(std::memory_order_relaxed),
                            g_deallocations.load(std::memory_order_relaxed),
                            g_bytes_allocated.load(std::memory_order_relaxed)};
}

void set_allocation_tracking_enabled(bool enabled) noexcept {
  g_tracking_enabled.store(enabled, std::memory_order_relaxed);
}

bool allocation_tracking_enabled() noexcept {
  return g_tracking_enabled.load(std::memory_order_relaxed);
}

ScopedAllocationCounter::ScopedAllocationCounter(bool reset) noexcept
    : previous_enabled_(allocation_tracking_enabled()) {
  set_allocation_tracking_enabled(true);
  if (reset) {
    reset_allocation_counters();
  }
}

ScopedAllocationCounter::~ScopedAllocationCounter() noexcept {
  set_allocation_tracking_enabled(previous_enabled_);
}

AllocationSnapshot ScopedAllocationCounter::snapshot() const noexcept {
  return allocation_snapshot();
}

} // namespace asterion

namespace {

[[nodiscard]] void* allocate_unaligned(std::size_t size) {
  const std::size_t actual_size = size == 0 ? 1 : size;
  void* pointer = std::malloc(actual_size);
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
  asterion::allocation_tracker_detail::record_allocation(size);
  return pointer;
}

[[nodiscard]] void* allocate_aligned(std::size_t size, std::align_val_t alignment) {
  const std::size_t actual_size = size == 0 ? 1 : size;
  const std::size_t alignment_value = static_cast<std::size_t>(alignment);
  void* pointer = nullptr;
#if defined(_MSC_VER)
  pointer = _aligned_malloc(actual_size, alignment_value);
  if (pointer == nullptr) {
    throw std::bad_alloc();
  }
#else
  if (posix_memalign(&pointer, alignment_value, actual_size) != 0) {
    throw std::bad_alloc();
  }
#endif
  asterion::allocation_tracker_detail::record_allocation(size);
  return pointer;
}

void deallocate_aligned(void* pointer) noexcept {
#if defined(_MSC_VER)
  _aligned_free(pointer);
#else
  std::free(pointer);
#endif
}

} // namespace

void* operator new(std::size_t size) {
  return allocate_unaligned(size);
}

void* operator new[](std::size_t size) {
  return ::operator new(size);
}

void* operator new(std::size_t size, std::align_val_t alignment) {
  return allocate_aligned(size, alignment);
}

void* operator new[](std::size_t size, std::align_val_t alignment) {
  return ::operator new(size, alignment);
}

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return ::operator new(size);
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
  try {
    return ::operator new[](size);
  } catch (...) {
    return nullptr;
  }
}

void* operator new(std::size_t size, std::align_val_t alignment,
                   const std::nothrow_t&) noexcept {
  try {
    return ::operator new(size, alignment);
  } catch (...) {
    return nullptr;
  }
}

void* operator new[](std::size_t size, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
  try {
    return ::operator new[](size, alignment);
  } catch (...) {
    return nullptr;
  }
}

void operator delete(void* pointer) noexcept {
  if (pointer != nullptr) {
    asterion::allocation_tracker_detail::record_deallocation();
  }
  std::free(pointer);
}

void operator delete[](void* pointer) noexcept {
  ::operator delete(pointer);
}

void operator delete(void* pointer, std::size_t) noexcept {
  ::operator delete(pointer);
}

void operator delete[](void* pointer, std::size_t) noexcept {
  ::operator delete[](pointer);
}

void operator delete(void* pointer, std::align_val_t) noexcept {
  if (pointer != nullptr) {
    asterion::allocation_tracker_detail::record_deallocation();
  }
  deallocate_aligned(pointer);
}

void operator delete[](void* pointer, std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}

void operator delete(void* pointer, std::size_t, std::align_val_t alignment) noexcept {
  ::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, std::size_t, std::align_val_t alignment) noexcept {
  ::operator delete[](pointer, alignment);
}

void operator delete(void* pointer, const std::nothrow_t&) noexcept {
  ::operator delete(pointer);
}

void operator delete[](void* pointer, const std::nothrow_t&) noexcept {
  ::operator delete[](pointer);
}

void operator delete(void* pointer, std::align_val_t alignment,
                     const std::nothrow_t&) noexcept {
  ::operator delete(pointer, alignment);
}

void operator delete[](void* pointer, std::align_val_t alignment,
                       const std::nothrow_t&) noexcept {
  ::operator delete[](pointer, alignment);
}
