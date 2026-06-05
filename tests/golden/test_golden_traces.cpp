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

TEST_CASE("Scenario E: IOC partial fill has a final cancel and stable checksum",
          "[golden][matching][ioc]") {
  const auto run = []() {
    MatchingEngine engine(1);
    (void)engine.submit_order(
        NewOrderRequest{601, 1, Side::Sell, OrderType::Limit, 1001, 4, 1});
    NewOrderRequest ioc{602, 1, Side::Buy, OrderType::Limit, 1001, 10, 2};
    ioc.time_in_force = TimeInForce::Ioc;
    const auto reports = engine.submit_order(ioc);

    REQUIRE(reports.size() == 4);
    REQUIRE(reports[2].client_order_id == 602);
    REQUIRE(reports[2].exec_type == ExecType::Trade);
    REQUIRE(reports[2].filled_quantity == 4);
    REQUIRE(reports[3].client_order_id == 602);
    REQUIRE(reports[3].exec_type == ExecType::Canceled);
    REQUIRE(reports[3].order_status == OrderStatus::Canceled);
    REQUIRE(engine.book().empty());
    return engine.reports_checksum();
  };

  REQUIRE(run() == run());
}

TEST_CASE("Scenario F: failed and successful FOK traces are atomic", "[golden][matching][fok]") {
  MatchingEngine engine(1);
  (void)engine.submit_order(
      NewOrderRequest{701, 1, Side::Sell, OrderType::Limit, 1001, 5, 1});
  const std::uint64_t before_failed_fok = engine.book().checksum();

  NewOrderRequest failed{702, 1, Side::Buy, OrderType::Limit, 1001, 6, 2};
  failed.time_in_force = TimeInForce::Fok;
  const auto failed_reports = engine.submit_order(failed);
  REQUIRE(failed_reports.size() == 1);
  REQUIRE(failed_reports.front().reject_reason == RejectReason::FokNotFillable);
  REQUIRE(engine.book().checksum() == before_failed_fok);

  NewOrderRequest success{703, 1, Side::Buy, OrderType::Limit, 1001, 5, 3};
  success.time_in_force = TimeInForce::Fok;
  const auto success_reports = engine.submit_order(success);
  REQUIRE(success_reports.size() == 3);
  REQUIRE(success_reports.back().order_status == OrderStatus::Filled);
  REQUIRE(engine.book().empty());
}

TEST_CASE("Scenario G: post-only and self-trade rejects do not mutate the book",
          "[golden][matching][post-only][stp]") {
  MatchingEngine engine(1);
  (void)engine.submit_order(
      NewOrderRequest{801, 1, Side::Sell, OrderType::Limit, 1001, 10, 1, 7});
  const std::uint64_t checksum = engine.book().checksum();

  NewOrderRequest post_only{802, 1, Side::Buy, OrderType::Limit, 1001, 10, 2, 8};
  post_only.post_only = true;
  const auto post_only_reports = engine.submit_order(post_only);
  REQUIRE(post_only_reports.front().reject_reason == RejectReason::PostOnlyWouldCross);
  REQUIRE(engine.book().checksum() == checksum);

  const auto self_reports = engine.submit_order(
      NewOrderRequest{803, 1, Side::Buy, OrderType::Limit, 1001, 10, 3, 7});
  REQUIRE(self_reports.front().reject_reason == RejectReason::SelfTradePrevention);
  REQUIRE(engine.book().checksum() == checksum);
}

TEST_CASE("Scenario H: replace at the same price loses FIFO priority", "[golden][matching][replace]") {
  MatchingEngine engine(1);
  const OrderId first_id =
      engine
          .submit_order(NewOrderRequest{901, 1, Side::Buy, OrderType::Limit, 1000, 10, 1})
          .front()
          .exchange_order_id;
  const OrderId second_id =
      engine
          .submit_order(NewOrderRequest{902, 1, Side::Buy, OrderType::Limit, 1000, 10, 2})
          .front()
          .exchange_order_id;

  const auto replace = engine.replace_order(ReplaceOrderRequest{903, first_id, 1000, 10, 3});
  REQUIRE(replace.front().exec_type == ExecType::Replaced);
  REQUIRE(engine.book().best_order(Side::Buy)->order_id == second_id);

  const auto sell = engine.submit_order(
      NewOrderRequest{904, 1, Side::Sell, OrderType::Market, 0, 10, 4});
  REQUIRE(sell[1].exchange_order_id == second_id);
  REQUIRE(engine.book().find_order(first_id) != nullptr);
}
