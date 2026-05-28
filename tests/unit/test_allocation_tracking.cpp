#include "asterion/book/order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace asterion;

namespace {

Order make_order(OrderId order_id, Side side, PriceTicks price, Quantity quantity) {
  return Order{order_id, order_id + 10'000, 1, side, price, quantity,
               static_cast<TimestampNs>(order_id), order_id};
}

} // namespace

TEST_CASE("Allocation tracker records standard allocations", "[alloc]") {
  std::vector<int> values;

  reset_allocation_counters();
  values.reserve(16);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(snapshot.allocations >= 1);
  REQUIRE(snapshot.bytes_allocated >= sizeof(int) * 16U);
}

TEST_CASE("Warmed cancel path does not allocate", "[alloc][book]") {
  OrderBook book(1);
  REQUIRE(book.add_order(make_order(1, Side::Buy, 1000, 10)));
  REQUIRE(book.add_order(make_order(2, Side::Buy, 1000, 10)));

  reset_allocation_counters();
  const bool canceled = book.cancel_order(1);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(canceled);
  REQUIRE(snapshot.allocations == 0);
}

TEST_CASE("L2 snapshot currently allocates caller-owned vectors", "[alloc][book]") {
  OrderBook book(1);
  for (OrderId order_id = 1; order_id <= 4; ++order_id) {
    REQUIRE(book.add_order(make_order(order_id, Side::Buy,
                                      1000 - static_cast<PriceTicks>(order_id), 10)));
    REQUIRE(book.add_order(make_order(order_id + 100, Side::Sell,
                                      1001 + static_cast<PriceTicks>(order_id), 10)));
  }

  reset_allocation_counters();
  const L2View view = book.l2_view(4);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(view.bids.size() == 4);
  REQUIRE(view.asks.size() == 4);
  REQUIRE(snapshot.allocations > 0);
}

TEST_CASE("Duplicate risk check reject path does not allocate after warm-up", "[alloc][risk]") {
  RiskGateway risk;
  risk.on_market_data(1, 1000, 10);
  const NewOrderRequest request{100, 1, Side::Buy, OrderType::Limit, 1000, 10, 11};
  REQUIRE(risk.check_new_order(request, 11).accepted);

  reset_allocation_counters();
  const RiskResult duplicate = risk.check_new_order(request, 12);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE_FALSE(duplicate.accepted);
  REQUIRE(duplicate.reject_reason == RejectReason::DuplicateClientOrderId);
  REQUIRE(snapshot.allocations == 0);
}

TEST_CASE("Linear inference score does not allocate after model construction", "[alloc][inference]") {
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  const std::array<double, 4> features{2.0, 1000.0, 0.35, 400.0};

  reset_allocation_counters();
  const double score = model.score(features);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(score == Catch::Approx(1.74));
  REQUIRE(snapshot.allocations == 0);
}
