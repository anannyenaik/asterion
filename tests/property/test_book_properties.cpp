#include "asterion/book/order_book.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstddef>
#include <random>
#include <vector>

using namespace asterion;

namespace {

enum class OpKind { Add, Cancel, Replace };

struct Op {
  OpKind kind{OpKind::Add};
  OrderId order_id{0};
  Side side{Side::Buy};
  PriceTicks price{0};
  Quantity quantity{0};
  PriceTicks new_price{0};
  Quantity new_quantity{0};
};

std::uint64_t apply_ops(const std::vector<Op>& ops) {
  OrderBook book(1);
  SequenceNumber sequence = 1;
  for (const Op& op : ops) {
    if (op.kind == OpKind::Add) {
      REQUIRE(book.add_order(Order{op.order_id, op.order_id + 10'000, 1, op.side, op.price,
                                   op.quantity, static_cast<TimestampNs>(sequence), sequence}));
    } else if (op.kind == OpKind::Cancel) {
      REQUIRE(book.cancel_order(op.order_id));
    } else {
      REQUIRE(book.replace_order(op.order_id, op.new_price, op.new_quantity,
                                 static_cast<TimestampNs>(sequence), sequence));
    }

    const auto report = book.check_invariants();
    INFO(report.violations.empty() ? "" : report.violations.front());
    REQUIRE(report.ok);
    ++sequence;
  }
  return book.checksum();
}

} // namespace

TEST_CASE("Randomized order-book streams preserve invariants and deterministic checksum",
          "[property][book]") {
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> price_dist(990, 1010);
  std::uniform_int_distribution<int> qty_dist(1, 500);
  std::uniform_int_distribution<int> op_dist(0, 99);

  std::vector<Op> ops;
  std::vector<OrderId> active;
  OrderId next_order_id = 1;

  for (std::size_t i = 0; i < 400; ++i) {
    const bool should_add = active.empty() || op_dist(rng) < 55;
    if (should_add) {
      const OrderId order_id = next_order_id++;
      active.push_back(order_id);
      ops.push_back(Op{OpKind::Add, order_id, side_dist(rng) == 0 ? Side::Buy : Side::Sell,
                       price_dist(rng), qty_dist(rng), 0, 0});
      continue;
    }

    std::uniform_int_distribution<std::size_t> active_dist(0, active.size() - 1U);
    const std::size_t index = active_dist(rng);
    const OrderId order_id = active[index];
    if (op_dist(rng) < 50) {
      ops.push_back(Op{OpKind::Cancel, order_id});
      active.erase(active.begin() + static_cast<std::ptrdiff_t>(index));
    } else {
      ops.push_back(Op{OpKind::Replace, order_id, Side::None, 0, 0, price_dist(rng),
                       qty_dist(rng)});
    }
  }

  const std::uint64_t checksum_a = apply_ops(ops);
  const std::uint64_t checksum_b = apply_ops(ops);
  REQUIRE(checksum_a == checksum_b);
}
