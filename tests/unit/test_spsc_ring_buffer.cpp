#include "asterion/concurrency/spsc_ring_buffer.hpp"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

using namespace asterion;

TEST_CASE("spsc ring buffer reports requested capacity", "[spsc][queue]") {
  SpscRingBuffer<int> queue(8);
  CHECK(queue.capacity() == 8);
  CHECK(queue.empty_approx());
  CHECK(queue.size_approx() == 0);
}

TEST_CASE("spsc ring buffer pop on empty fails without modifying output", "[spsc][queue]") {
  SpscRingBuffer<int> queue(4);
  int value = 1234;
  CHECK_FALSE(queue.try_pop(value));
  CHECK(value == 1234);
}

TEST_CASE("spsc ring buffer fills to capacity then reports full", "[spsc][queue]") {
  SpscRingBuffer<int> queue(3);
  CHECK(queue.try_push(10));
  CHECK(queue.try_push(20));
  CHECK(queue.try_push(30));
  CHECK(queue.size_approx() == 3);
  // Full: the reserved slot keeps usable capacity at exactly 3.
  CHECK_FALSE(queue.try_push(40));

  int value = 0;
  CHECK(queue.try_pop(value));
  CHECK(value == 10);
  // A freed slot allows exactly one more push.
  CHECK(queue.try_push(40));
  CHECK_FALSE(queue.try_push(50));
}

TEST_CASE("spsc ring buffer preserves FIFO order", "[spsc][queue]") {
  SpscRingBuffer<int> queue(4);
  for (int i = 0; i < 4; ++i) {
    REQUIRE(queue.try_push(i));
  }
  for (int i = 0; i < 4; ++i) {
    int value = -1;
    REQUIRE(queue.try_pop(value));
    CHECK(value == i);
  }
  CHECK(queue.empty_approx());
}

TEST_CASE("spsc ring buffer wraps around correctly across many cycles", "[spsc][queue]") {
  SpscRingBuffer<int> queue(2);
  int expected = 0;
  for (int round = 0; round < 1000; ++round) {
    REQUIRE(queue.try_push(round));
    int value = -1;
    REQUIRE(queue.try_pop(value));
    CHECK(value == expected);
    ++expected;
  }
}

TEST_CASE("spsc ring buffer transfers every item across two threads", "[spsc][queue][thread]") {
  constexpr int kCount = 100'000;
  SpscRingBuffer<int> queue(64);

  std::thread producer([&] {
    for (int i = 0; i < kCount; ++i) {
      while (!queue.try_push(i)) {
        std::this_thread::yield();
      }
    }
  });

  std::uint64_t sum = 0;
  int received = 0;
  int value = 0;
  while (received < kCount) {
    if (queue.try_pop(value)) {
      // Items must arrive strictly in order.
      REQUIRE(value == received);
      sum += static_cast<std::uint64_t>(value);
      ++received;
    } else {
      std::this_thread::yield();
    }
  }
  producer.join();

  CHECK(received == kCount);
  CHECK(sum == static_cast<std::uint64_t>(kCount) * (kCount - 1) / 2);
}
