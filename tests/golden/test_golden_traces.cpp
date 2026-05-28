#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace asterion;

TEST_CASE("Scenario A: FIFO sell queue is consumed by buy market order", "[golden]") {
  MatchingEngine engine(1);

  const auto sell_a = engine.submit_order(
      NewOrderRequest{101, 1, Side::Sell, OrderType::Limit, 1001, 100, 1});
  const auto sell_b = engine.submit_order(
      NewOrderRequest{102, 1, Side::Sell, OrderType::Limit, 1001, 50, 2});
  const OrderId sell_a_id = sell_a.front().exchange_order_id;
  const OrderId sell_b_id = sell_b.front().exchange_order_id;

  const auto buy = engine.submit_order(
      NewOrderRequest{201, 1, Side::Buy, OrderType::Market, 0, 120, 3});

  REQUIRE(buy.size() == 5);
  REQUIRE(buy[1].exchange_order_id == sell_a_id);
  REQUIRE(buy[1].order_status == OrderStatus::Filled);
  REQUIRE(buy[1].last_fill_quantity == 100);
  REQUIRE(buy[3].exchange_order_id == sell_b_id);
  REQUIRE(buy[3].order_status == OrderStatus::PartiallyFilled);
  REQUIRE(buy[3].last_fill_quantity == 20);

  REQUIRE(engine.book().find_order(sell_a_id) == nullptr);
  const Order* remaining = engine.book().find_order(sell_b_id);
  REQUIRE(remaining != nullptr);
  REQUIRE(remaining->quantity == 30);
  REQUIRE(engine.book().total_quantity_at(Side::Sell, 1001) == 30);
  REQUIRE(engine.book().check_invariants().ok);
}

TEST_CASE("Scenario B: replace reprices a buy before a sell crosses", "[golden]") {
  MatchingEngine engine(1);

  const auto buy = engine.submit_order(
      NewOrderRequest{301, 1, Side::Buy, OrderType::Limit, 999, 100, 1});
  const OrderId buy_id = buy.front().exchange_order_id;
  const auto replace = engine.replace_order(ReplaceOrderRequest{302, buy_id, 1000, 100, 2});
  REQUIRE(replace.size() == 1);
  REQUIRE(replace.front().exec_type == ExecType::Replaced);

  const auto sell = engine.submit_order(
      NewOrderRequest{303, 1, Side::Sell, OrderType::Limit, 1000, 100, 3});

  REQUIRE(sell.size() == 3);
  REQUIRE(sell[1].exchange_order_id == buy_id);
  REQUIRE(sell[1].exec_type == ExecType::Trade);
  REQUIRE(sell[1].last_fill_price_ticks == 1000);
  REQUIRE(sell[2].client_order_id == 303);
  REQUIRE(sell[2].order_status == OrderStatus::Filled);
  REQUIRE(engine.book().empty());
  REQUIRE(engine.book().check_invariants().ok);
}

TEST_CASE("Scenario C: duplicate client order ID is rejected", "[golden][risk]") {
  RiskGateway risk;
  risk.on_market_data(1, 1000, 10);

  const NewOrderRequest request{401, 1, Side::Buy, OrderType::Limit, 1000, 10, 11};
  const auto first = risk.check_new_order(request, 11);
  const auto second = risk.check_new_order(request, 12);

  REQUIRE(first.accepted);
  REQUIRE_FALSE(second.accepted);
  REQUIRE(second.reject_reason == RejectReason::DuplicateClientOrderId);
}

TEST_CASE("Scenario D: kill switch rejects new orders", "[golden][risk]") {
  RiskGateway risk;
  risk.on_market_data(1, 1000, 10);
  risk.enable_kill_switch();

  const auto result = risk.check_new_order(
      NewOrderRequest{501, 1, Side::Sell, OrderType::Limit, 1001, 10, 11}, 11);

  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.reject_reason == RejectReason::KillSwitch);
}
