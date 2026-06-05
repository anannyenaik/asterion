#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace asterion;

namespace {

NewOrderRequest order(ClientOrderId id, Side side, OrderType type, PriceTicks price,
                      Quantity quantity, TimestampNs timestamp, ClientId client_id = 0,
                      TimeInForce time_in_force = TimeInForce::Gtc, bool post_only = false) {
  NewOrderRequest request{id, 1, side, type, price, quantity, timestamp, client_id};
  request.time_in_force = time_in_force;
  request.post_only = post_only;
  return request;
}

OrderId accepted_order_id(const std::vector<ExecutionReport>& reports) {
  REQUIRE_FALSE(reports.empty());
  REQUIRE(reports.front().exec_type == ExecType::New);
  return reports.front().exchange_order_id;
}

} // namespace

TEST_CASE("IOC limit orders fill immediately and cancel any remainder", "[matching][ioc]") {
  SECTION("full fill") {
    MatchingEngine engine(1);
    (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 10, 1));

    const auto reports =
        engine.submit_order(order(2, Side::Buy, OrderType::Limit, 1001, 10, 2, 0,
                                  TimeInForce::Ioc));

    REQUIRE(reports.size() == 3);
    REQUIRE(reports.back().order_status == OrderStatus::Filled);
    REQUIRE(reports.back().filled_quantity == 10);
    REQUIRE(engine.book().empty());
  }

  SECTION("partial fill cancels remainder") {
    MatchingEngine engine(1);
    (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 4, 1));

    const auto reports =
        engine.submit_order(order(2, Side::Buy, OrderType::Limit, 1001, 10, 2, 0,
                                  TimeInForce::Ioc));
    const OrderId ioc_id = reports.front().exchange_order_id;

    REQUIRE(reports.back().exec_type == ExecType::Canceled);
    REQUIRE(reports.back().order_status == OrderStatus::Canceled);
    REQUIRE(reports.back().filled_quantity == 4);
    REQUIRE(reports.back().remaining_quantity == 0);
    REQUIRE(engine.book().find_order(ioc_id) == nullptr);
    REQUIRE(engine.book().empty());
  }

  SECTION("no liquidity produces a deterministic terminal cancel") {
    MatchingEngine first(1);
    MatchingEngine second(1);
    const auto request = order(1, Side::Buy, OrderType::Limit, 1000, 7, 1, 0,
                               TimeInForce::Ioc);

    const auto first_reports = first.submit_order(request);
    const auto second_reports = second.submit_order(request);

    REQUIRE(first_reports.size() == 2);
    REQUIRE(first_reports.back().exec_type == ExecType::Canceled);
    REQUIRE(first_reports.back().filled_quantity == 0);
    REQUIRE(first.book().empty());
    REQUIRE(first.reports_checksum() == second.reports_checksum());
  }

  SECTION("limit price is respected") {
    MatchingEngine engine(1);
    const auto ask = engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1002, 10, 1));
    const OrderId ask_id = accepted_order_id(ask);

    const auto reports =
        engine.submit_order(order(2, Side::Buy, OrderType::Limit, 1001, 10, 2, 0,
                                  TimeInForce::Ioc));

    REQUIRE(reports.back().exec_type == ExecType::Canceled);
    REQUIRE(reports.back().filled_quantity == 0);
    REQUIRE(engine.book().find_order(ask_id) != nullptr);
  }
}

TEST_CASE("Unsupported matching request combinations reject deterministically",
          "[matching][reject]") {
  MatchingEngine engine(1);

  auto invalid_type = order(1, Side::Buy, OrderType::Limit, 1000, 10, 1);
  invalid_type.order_type = static_cast<OrderType>(99);
  const auto invalid_type_reports = engine.submit_order(invalid_type);
  REQUIRE(invalid_type_reports.front().reject_reason == RejectReason::Unsupported);

  const auto post_only_ioc =
      engine.submit_order(order(2, Side::Buy, OrderType::Limit, 1000, 10, 2, 0,
                                TimeInForce::Ioc, true));
  REQUIRE(post_only_ioc.front().reject_reason == RejectReason::Unsupported);
  REQUIRE(engine.book().empty());
}

TEST_CASE("FOK orders either fill completely or leave the book unchanged", "[matching][fok]") {
  SECTION("fully fillable FOK follows price-time priority") {
    MatchingEngine engine(1);
    const OrderId first_id = accepted_order_id(
        engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 4, 1)));
    const OrderId second_id = accepted_order_id(
        engine.submit_order(order(2, Side::Sell, OrderType::Limit, 1001, 6, 2)));

    const auto reports =
        engine.submit_order(order(3, Side::Buy, OrderType::Limit, 1001, 10, 3, 0,
                                  TimeInForce::Fok));

    REQUIRE(reports.size() == 5);
    REQUIRE(reports[1].exchange_order_id == first_id);
    REQUIRE(reports[3].exchange_order_id == second_id);
    REQUIRE(reports.back().order_status == OrderStatus::Filled);
    REQUIRE(engine.book().empty());
  }

  SECTION("insufficient quantity rejects without partial fill or mutation") {
    MatchingEngine engine(1);
    (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 5, 1));
    const std::uint64_t checksum_before = engine.book().checksum();

    const auto reports =
        engine.submit_order(order(2, Side::Buy, OrderType::Limit, 1001, 6, 2, 0,
                                  TimeInForce::Fok));

    REQUIRE(reports.size() == 1);
    REQUIRE(reports.front().order_status == OrderStatus::Rejected);
    REQUIRE(reports.front().reject_reason == RejectReason::FokNotFillable);
    REQUIRE(reports.front().filled_quantity == 0);
    REQUIRE(engine.book().checksum() == checksum_before);
  }

  SECTION("limit price excludes worse liquidity") {
    MatchingEngine engine(1);
    (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 4, 1));
    (void)engine.submit_order(order(2, Side::Sell, OrderType::Limit, 1002, 6, 2));
    const std::uint64_t checksum_before = engine.book().checksum();

    const auto reports =
        engine.submit_order(order(3, Side::Buy, OrderType::Limit, 1001, 10, 3, 0,
                                  TimeInForce::Fok));

    REQUIRE(reports.front().reject_reason == RejectReason::FokNotFillable);
    REQUIRE(engine.book().checksum() == checksum_before);
  }

  SECTION("market FOK fills across all available prices") {
    MatchingEngine engine(1);
    (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 4, 1));
    (void)engine.submit_order(order(2, Side::Sell, OrderType::Limit, 1002, 6, 2));

    const auto reports =
        engine.submit_order(order(3, Side::Buy, OrderType::Market, 0, 10, 3, 0,
                                  TimeInForce::Fok));

    REQUIRE(reports.back().order_status == OrderStatus::Filled);
    REQUIRE(reports.back().filled_quantity == 10);
    REQUIRE(engine.book().empty());
  }
}

TEST_CASE("Post-only limits rest or reject before trading", "[matching][post-only]") {
  MatchingEngine engine(1);
  (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1002, 10, 1));
  (void)engine.submit_order(order(2, Side::Buy, OrderType::Limit, 999, 10, 2));

  const auto resting_buy =
      engine.submit_order(order(3, Side::Buy, OrderType::Limit, 1001, 5, 3, 0,
                                TimeInForce::Gtc, true));
  const auto resting_sell =
      engine.submit_order(order(4, Side::Sell, OrderType::Limit, 1003, 5, 4, 0,
                                TimeInForce::Gtc, true));
  REQUIRE(engine.book().find_order(accepted_order_id(resting_buy)) != nullptr);
  REQUIRE(engine.book().find_order(accepted_order_id(resting_sell)) != nullptr);

  const std::uint64_t checksum_before = engine.book().checksum();
  const auto crossing_buy =
      engine.submit_order(order(5, Side::Buy, OrderType::Limit, 1002, 5, 5, 0,
                                TimeInForce::Gtc, true));
  const auto crossing_sell =
      engine.submit_order(order(6, Side::Sell, OrderType::Limit, 1001, 5, 6, 0,
                                TimeInForce::Gtc, true));

  REQUIRE(crossing_buy.size() == 1);
  REQUIRE(crossing_buy.front().reject_reason == RejectReason::PostOnlyWouldCross);
  REQUIRE(crossing_sell.front().reject_reason == RejectReason::PostOnlyWouldCross);
  REQUIRE(engine.book().checksum() == checksum_before);
}

TEST_CASE("Matching self-trade prevention rejects attributed incoming flow", "[matching][stp]") {
  SECTION("normal limit and different-owner behavior") {
    MatchingEngine engine(1);
    (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 10, 1, 7));
    const std::uint64_t checksum_before = engine.book().checksum();

    const auto self =
        engine.submit_order(order(2, Side::Buy, OrderType::Limit, 1001, 10, 2, 7));
    REQUIRE(self.front().reject_reason == RejectReason::SelfTradePrevention);
    REQUIRE(engine.book().checksum() == checksum_before);

    const auto other =
        engine.submit_order(order(3, Side::Buy, OrderType::Limit, 1001, 10, 3, 8));
    REQUIRE(other.back().order_status == OrderStatus::Filled);
    REQUIRE(engine.book().empty());
  }

  SECTION("market IOC and FOK are rejected before matching") {
    for (const auto [type, tif] :
         {std::pair{OrderType::Market, TimeInForce::Gtc},
          std::pair{OrderType::Limit, TimeInForce::Ioc},
          std::pair{OrderType::Limit, TimeInForce::Fok}}) {
      MatchingEngine engine(1);
      (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 10, 1, 7));
      const std::uint64_t checksum_before = engine.book().checksum();
      const PriceTicks price = type == OrderType::Market ? 0 : 1001;

      const auto reports = engine.submit_order(order(2, Side::Buy, type, price, 10, 2, 7, tif));

      REQUIRE(reports.front().reject_reason == RejectReason::SelfTradePrevention);
      REQUIRE(engine.book().checksum() == checksum_before);
    }
  }

  SECTION("crossing post-only rejects as post-only because it cannot trade") {
    MatchingEngine engine(1);
    (void)engine.submit_order(order(1, Side::Sell, OrderType::Limit, 1001, 10, 1, 7));
    const auto reports =
        engine.submit_order(order(2, Side::Buy, OrderType::Limit, 1001, 10, 2, 7,
                                  TimeInForce::Gtc, true));
    REQUIRE(reports.front().reject_reason == RejectReason::PostOnlyWouldCross);
  }

  SECTION("replace that would self-cross is rejected without losing original priority") {
    MatchingEngine engine(1);
    const OrderId buy_id = accepted_order_id(
        engine.submit_order(order(1, Side::Buy, OrderType::Limit, 999, 10, 1, 7)));
    (void)engine.submit_order(order(2, Side::Sell, OrderType::Limit, 1002, 10, 2, 7));
    const std::uint64_t checksum_before = engine.book().checksum();

    const auto reports = engine.replace_order(ReplaceOrderRequest{3, buy_id, 1002, 10, 3});

    REQUIRE(reports.front().reject_reason == RejectReason::SelfTradePrevention);
    REQUIRE(engine.book().checksum() == checksum_before);
  }
}

TEST_CASE("Risk working exposure only tracks restable GTC limits", "[risk][matching-semantics]") {
  RiskLimits limits;
  limits.max_open_order_quantity = 10;
  RiskGateway risk(limits);
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(
                  order(1, Side::Buy, OrderType::Limit, 1000, 100, 1, 7, TimeInForce::Ioc), 1)
              .accepted);
  REQUIRE(risk.check_new_order(
                  order(2, Side::Buy, OrderType::Limit, 1000, 100, 2, 7, TimeInForce::Fok), 2)
              .accepted);
  REQUIRE(risk.working_quantity(1) == 0);

  const auto gtc = risk.check_new_order(order(3, Side::Buy, OrderType::Limit, 1000, 11, 3, 7), 3);
  REQUIRE_FALSE(gtc.accepted);
  REQUIRE(gtc.reject_reason == RejectReason::MaxOpenOrderQuantity);
}

TEST_CASE("Risk-first STP and matching post-only policy compose deterministically",
          "[risk][matching][stp][post-only]") {
  RiskLimits limits;
  limits.enable_self_trade_prevention = true;
  RiskGateway risk(limits);
  MatchingEngine engine(1);
  risk.on_market_data(1, 1000, 0);

  const auto resting = order(1, Side::Sell, OrderType::Limit, 1001, 10, 1, 7);
  REQUIRE(risk.check_new_order(resting, 1).accepted);
  const auto resting_reports = engine.submit_order(resting);
  risk.on_execution_reports(resting_reports);
  REQUIRE(risk.working_quantity(1) == 10);

  const auto normal_cross = order(2, Side::Buy, OrderType::Limit, 1001, 10, 2, 7);
  REQUIRE_FALSE(risk.check_new_order(normal_cross, 2).accepted);
  REQUIRE(engine.book().order_count() == 1);

  const auto post_only_cross =
      order(3, Side::Buy, OrderType::Limit, 1001, 10, 3, 7, TimeInForce::Gtc, true);
  REQUIRE(risk.check_new_order(post_only_cross, 3).accepted);
  const auto post_only_reports = engine.submit_order(post_only_cross);
  REQUIRE(post_only_reports.front().reject_reason == RejectReason::PostOnlyWouldCross);
  risk.on_execution_reports(post_only_reports);
  REQUIRE(risk.working_quantity(1) == 10);
  REQUIRE(engine.book().order_count() == 1);
}
