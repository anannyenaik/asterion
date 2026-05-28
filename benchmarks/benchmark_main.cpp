#include "asterion/book/order_book.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/matching/matching_engine.hpp"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

using namespace asterion;

namespace {

template <typename Fn> std::uint64_t elapsed_ns(Fn&& fn) {
  const auto start = std::chrono::steady_clock::now();
  fn();
  const auto end = std::chrono::steady_clock::now();
  return static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
}

void print_result(const std::string& name, std::size_t iterations, std::uint64_t total_ns,
                  std::uint64_t guard) {
  const auto average = iterations == 0 ? 0 : total_ns / iterations;
  std::cout << name << ",iterations=" << iterations << ",total_ns=" << total_ns
            << ",avg_ns=" << average << ",guard=" << guard << '\n';
}

Order make_order(OrderId order_id, Side side, PriceTicks price, Quantity quantity) {
  return Order{order_id, order_id + 1'000'000, 1, side, price, quantity,
               static_cast<TimestampNs>(order_id), order_id};
}

} // namespace

int main(int argc, char** argv) {
  const std::size_t iterations = 50'000;
  std::uint64_t guard = 0;

  {
    OrderBook book(1);
    const auto total = elapsed_ns([&] {
      for (std::size_t i = 0; i < iterations; ++i) {
        const OrderId order_id = static_cast<OrderId>(i + 1U);
        book.add_order(make_order(order_id, Side::Buy, 1000, 10));
      }
    });
    guard ^= book.checksum();
    print_result("add_order", iterations, total, guard);
  }

  {
    OrderBook book(1);
    for (std::size_t i = 0; i < iterations; ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1U);
      book.add_order(make_order(order_id, Side::Sell, 1001, 10));
    }
    const auto total = elapsed_ns([&] {
      for (std::size_t i = 0; i < iterations; ++i) {
        const OrderId order_id = static_cast<OrderId>(i + 1U);
        book.cancel_order(order_id);
      }
    });
    guard ^= book.checksum();
    print_result("cancel_order", iterations, total, guard);
  }

  {
    const auto total = elapsed_ns([&] {
      for (std::size_t i = 0; i < 10'000; ++i) {
        MatchingEngine engine(1);
        engine.submit_order(NewOrderRequest{1, 1, Side::Sell, OrderType::Limit, 1001, 10, 1});
        engine.submit_order(NewOrderRequest{2, 1, Side::Buy, OrderType::Market, 0, 10, 2});
        guard ^= engine.reports_checksum();
      }
    });
    print_result("simple_match", 10'000, total, guard);
  }

  {
    std::filesystem::path replay_path =
        argc > 1 ? std::filesystem::path(argv[1])
                 : std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples" /
                       "sample_replay.csv";
    const auto total = elapsed_ns([&] {
      ReplayEngine replay(1);
      const ReplayResult result = replay.replay_file(replay_path);
      guard ^= result.final_book_checksum;
      guard ^= result.execution_report_checksum;
    });
    print_result("sample_replay", 1, total, guard);
  }

  return 0;
}
