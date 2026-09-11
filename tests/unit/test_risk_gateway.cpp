#include "asterion/risk/risk_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace asterion;

TEST_CASE("Risk gateway accepts an order within limits", "[risk]") {
  RiskGateway risk(RiskLimits{1'000, 2'000'000, 5'000, 10'000'000, 50, 1'000});
  risk.on_market_data(1, 1000, 100);

  const auto result = risk.check_new_order(
      NewOrderRequest{10, 1, Side::Buy, OrderType::Limit, 1001, 100, 101}, 101);

  REQUIRE(result.accepted);
  REQUIRE(result.reject_reason == RejectReason::None);
}

TEST_CASE("Risk gateway rejects price-band and stale-market-data breaches", "[risk]") {
  RiskGateway risk(RiskLimits{1'000, 2'000'000, 5'000, 10'000'000, 10, 5});
  risk.on_market_data(1, 1000, 100);

  const auto price_band = risk.check_new_order(
      NewOrderRequest{11, 1, Side::Buy, OrderType::Limit, 1020, 10, 101}, 101);
  REQUIRE_FALSE(price_band.accepted);
  REQUIRE(price_band.reject_reason == RejectReason::PriceBand);

  const auto stale = risk.check_new_order(
      NewOrderRequest{12, 1, Side::Buy, OrderType::Limit, 1000, 10, 200}, 200);
  REQUIRE_FALSE(stale.accepted);
  REQUIRE(stale.reject_reason == RejectReason::StaleMarketData);
}
