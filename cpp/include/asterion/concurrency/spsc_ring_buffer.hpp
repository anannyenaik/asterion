#pragma once

#include <atomic>
#include <cstddef>
#include <vector>

namespace asterion {

// Bounded single-producer / single-consumer (SPSC) ring buffer.
//
// Scope and guarantees:
//   * EXACTLY ONE producer thread may call try_push.
//   * EXACTLY ONE consumer thread may call try_pop.
//   * The two roles may run concurrently; no other concurrent access is safe.
//   * This is NOT a general MPMC queue and must not be used as one.
//
// The element type is fixed at construction (template parameter T) and must be
// trivially/cheaply copyable. Capacity is fixed at construction. The backing
// storage is allocated exactly once in the constructor; no allocation happens
// during try_push / try_pop, so steady-state operation is allocation-free.
//
// Memory ordering rationale:
//   The buffer is a classic Lamport ring with one reserved slot to distinguish
//   the full and empty states. The producer owns tail_; the consumer owns head_.
//   * try_push reads head_ with acquire so it observes slots the consumer has
//     freed, writes the element, then publishes the new tail_ with release so the
//     consumer that later acquires tail_ sees a fully-written element.
//   * try_pop reads tail_ with acquire so it observes elements the producer has
//     published, reads the element, then publishes the new head_ with release so
//     the producer that later acquires head_ sees the slot as free.
//   Each index is written by a single thread, so plain stores (paired with the
//   release fence) are sufficient; no read-modify-write atomics are required.
template <typename T>
class SpscRingBuffer {
public:
  explicit SpscRingBuffer(std::size_t capacity)
      // One extra slot is reserved so a full buffer is distinguishable from an
      // empty one without a separate count. Usable capacity == requested capacity.
      : slots_(capacity + 1U), capacity_(capacity) {}

  SpscRingBuffer(const SpscRingBuffer&) = delete;
  SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;
  SpscRingBuffer(SpscRingBuffer&&) = delete;
  SpscRingBuffer& operator=(SpscRingBuffer&&) = delete;
  ~SpscRingBuffer() = default;

  // Producer side. Returns false without modifying state when the buffer is full.
  [[nodiscard]] bool try_push(const T& item) noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t next = increment(tail);
    if (next == head_.load(std::memory_order_acquire)) {
      return false; // full
    }
    slots_[tail] = item;
    tail_.store(next, std::memory_order_release);
    return true;
  }

  // Consumer side. Returns false without modifying out when the buffer is empty.
  [[nodiscard]] bool try_pop(T& out) noexcept {
    const std::size_t head = head_.load(std::memory_order_relaxed);
    if (head == tail_.load(std::memory_order_acquire)) {
      return false; // empty
    }
    out = slots_[head];
    head_.store(increment(head), std::memory_order_release);
    return true;
  }

  [[nodiscard]] std::size_t capacity() const noexcept { return capacity_; }

  // Best-effort size snapshot for diagnostics/stats only. It is exact when read
  // from the producer thread immediately after a push (the producer owns tail_
  // and only reads a monotonically advancing head_); otherwise it is an estimate
  // and must never be used for correctness decisions.
  [[nodiscard]] std::size_t size_approx() const noexcept {
    const std::size_t tail = tail_.load(std::memory_order_relaxed);
    const std::size_t head = head_.load(std::memory_order_relaxed);
    return (tail + slots_.size() - head) % slots_.size();
  }

  [[nodiscard]] bool empty_approx() const noexcept {
    return head_.load(std::memory_order_relaxed) == tail_.load(std::memory_order_relaxed);
  }

private:
  [[nodiscard]] std::size_t increment(std::size_t index) const noexcept {
    return (index + 1U) % slots_.size();
  }

  // Fixed 64-byte cache-line assumption. We deliberately avoid
  // std::hardware_destructive_interference_size because its value is not stable
  // across compiler/-mtune settings (GCC warns about ABI drift); 64 bytes is the
  // common line size on the x86-64 targets this lab is exercised on.
  static constexpr std::size_t kCacheLine = 64;

  std::vector<T> slots_;
  std::size_t capacity_;
  // head_ and tail_ are placed on separate cache lines to avoid false sharing
  // between the producer (writes tail_) and the consumer (writes head_).
  alignas(kCacheLine) std::atomic<std::size_t> head_{0};
  alignas(kCacheLine) std::atomic<std::size_t> tail_{0};
};

} // namespace asterion
