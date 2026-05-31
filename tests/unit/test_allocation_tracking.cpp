#include "asterion/book/order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/strategy/imbalance_strategy.hpp"
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
#if defined(_WIN32)
  reset_allocation_counters();
  void* pointer = ::operator new(sizeof(int) * 16U);
  ::operator delete(pointer);
  const AllocationSnapshot snapshot = allocation_snapshot();
#else
  std::vector<int> values;

  reset_allocation_counters();
  values.reserve(16);
  const AllocationSnapshot snapshot = allocation_snapshot();
#endif

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

TEST_CASE("Reusable L2 snapshot storage does not allocate after reserve", "[alloc][book]") {
  OrderBook book(1);
  for (OrderId order_id = 1; order_id <= 4; ++order_id) {
    REQUIRE(book.add_order(make_order(order_id, Side::Buy,
                                      1000 - static_cast<PriceTicks>(order_id), 10)));
    REQUIRE(book.add_order(make_order(order_id + 100, Side::Sell,
                                      1001 + static_cast<PriceTicks>(order_id), 10)));
  }

  L2View view;
  view.reserve(4);
  book.fill_l2_view(4, view);

  reset_allocation_counters();
  book.fill_l2_view(4, view);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(view.bids.size() == 4);
  REQUIRE(view.asks.size() == 4);
  REQUIRE(snapshot.allocations == 0);
}

TEST_CASE("Fixed-size strategy callback does not allocate after L2 warm-up",
          "[alloc][strategy]") {
  OrderBook book(1);
  REQUIRE(book.add_order(make_order(1, Side::Buy, 1000, 100)));
  REQUIRE(book.add_order(make_order(2, Side::Sell, 1001, 20)));

  L2View view;
  view.reserve(1);
  book.fill_l2_view(1, view);
  ImbalanceStrategy strategy(0.50, 1);

  reset_allocation_counters();
  const StrategyDecisionBatch decisions = strategy.on_l2_update_fixed(view);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(decisions.size == 1);
  REQUIRE(decisions.decisions.front().side == Side::Buy);
  REQUIRE(snapshot.allocations == 0);
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

TEST_CASE("Reserved accepted risk check path does not allocate after warm-up", "[alloc][risk]") {
  RiskGateway risk;
  risk.reserve_hot_path_capacity(4, 1, 0);
  risk.on_market_data(1, 1000, 10);
  REQUIRE(risk.check_new_order(
              NewOrderRequest{100, 1, Side::Buy, OrderType::Limit, 1000, 1, 11}, 11)
              .accepted);

  reset_allocation_counters();
  const RiskResult accepted = risk.check_new_order(
      NewOrderRequest{101, 1, Side::Buy, OrderType::Limit, 1000, 1, 12}, 12);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(accepted.accepted);
  REQUIRE(snapshot.allocations == 0);
}

TEST_CASE("Warmed non-creating target path does not allocate", "[alloc][hot-path]") {
  OrderBook book(1);
  book.reserve_order_capacity(2);
  REQUIRE(book.add_order(make_order(1, Side::Buy, 1000, 100)));
  REQUIRE(book.add_order(make_order(2, Side::Sell, 1001, 90)));

  L2View view;
  view.reserve(1);
  ImbalanceStrategy strategy(0.50, 1);
  RiskGateway risk;
  risk.reserve_hot_path_capacity(4, 1, 0);
  risk.on_market_data(1, 1000, 10);
  REQUIRE(risk.check_new_order(
              NewOrderRequest{100, 1, Side::Buy, OrderType::Limit, 1000, 1, 11}, 11)
              .accepted);
  book.fill_l2_view(1, view);
  REQUIRE(strategy.on_l2_update_fixed(view).size == 1);

  reset_allocation_counters();
  REQUIRE(book.reduce_order(2, 1));
  book.fill_l2_view(1, view);
  const StrategyDecisionBatch decisions = strategy.on_l2_update_fixed(view);
  REQUIRE(decisions.size == 1);
  risk.on_market_data(1, 1000, 12);
  const RiskResult result = risk.check_new_order(
      NewOrderRequest{101, 1, decisions.decisions.front().side, OrderType::Limit,
                      decisions.decisions.front().price_ticks, 1, 12},
      12);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(result.accepted);
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

TEST_CASE("Caller-owned feature extraction and LinearModel scoring do not allocate after warm-up",
          "[alloc][inference][features]") {
  L2View view;
  view.reserve(1);
  view.bids.push_back(L2Level{999, 300});
  view.asks.push_back(L2Level{1001, 100});

  FeatureExtractor extractor;
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  std::array<double, kL2FeatureCount> storage{};
  FeatureBuffer buffer{storage};
  REQUIRE(extractor.extract_into(view, buffer) == FeatureExtractionStatus::Ok);
  REQUIRE(model.score(buffer.used()) == Catch::Approx(2.04));

  reset_allocation_counters();
  double score = 0.0;
  for (int i = 0; i < 16; ++i) {
    REQUIRE(extractor.extract_into(view, buffer) == FeatureExtractionStatus::Ok);
    score = model.score(buffer.used());
  }
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(score == Catch::Approx(2.04));
  REQUIRE(snapshot.allocations == 0);
}
