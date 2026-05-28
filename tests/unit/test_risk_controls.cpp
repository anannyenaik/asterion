#include "asterion/core/allocation_tracker.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/risk/risk_audit.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace asterion;

namespace {

NewOrderRequest order(ClientOrderId id, Side side, PriceTicks price, Quantity quantity,
                      TimestampNs now_ns, ClientId client_id) {
  return NewOrderRequest{id, 1, side, OrderType::Limit, price, quantity, now_ns, client_id};
}

RiskLimits all_controls_limits() {
  RiskLimits limits;
  limits.enable_self_trade_prevention = true;
  limits.max_open_order_quantity = 150;
  limits.max_messages_per_window = 5;
  limits.rate_window_ns = 1'000;
  return limits;
}

void populate_audit(RiskGateway& risk) {
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 0);
  (void)risk.check_new_order(order(1, Side::Sell, 1001, 100, 10, 7), 10);
  (void)risk.check_new_order(order(2, Side::Buy, 1001, 100, 20, 7), 20);  // self-trade reject
  (void)risk.check_new_order(order(3, Side::Sell, 1002, 100, 30, 7), 30); // working reject
}

} // namespace

TEST_CASE("Working-order exposure limit rejects and clears on release", "[risk][working]") {
  RiskLimits limits;
  limits.max_open_order_quantity = 150;
  RiskGateway risk(limits);
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 100);

  REQUIRE(risk.check_new_order(order(1, Side::Buy, 1000, 100, 101, 7), 101).accepted);
  REQUIRE(risk.working_quantity(1) == 100);

  const auto rejected = risk.check_new_order(order(2, Side::Buy, 1000, 100, 102, 7), 102);
  REQUIRE_FALSE(rejected.accepted);
  REQUIRE(rejected.reject_reason == RejectReason::MaxOpenOrderQuantity);
  REQUIRE(risk.working_quantity(1) == 100);

  const RiskAuditEntry& entry = risk.audit().entries().back();
  REQUIRE(entry.check_name == "max_open_order_quantity");
  REQUIRE(entry.limit_value == 150);
  REQUIRE(entry.observed_value == 200);

  risk.release_order(1);
  REQUIRE(risk.working_quantity(1) == 0);
  REQUIRE(risk.check_new_order(order(3, Side::Buy, 1000, 100, 103, 7), 103).accepted);
  REQUIRE(risk.working_quantity(1) == 100);
}

TEST_CASE("Message-rate limit is per client and resets after the window", "[risk][rate]") {
  RiskLimits limits;
  limits.max_messages_per_window = 2;
  limits.rate_window_ns = 100;
  RiskGateway risk(limits);
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(order(1, Side::Buy, 1000, 10, 10, 7), 10).accepted);
  REQUIRE(risk.check_new_order(order(2, Side::Buy, 1000, 10, 20, 7), 20).accepted);

  const auto throttled = risk.check_new_order(order(3, Side::Buy, 1000, 10, 30, 7), 30);
  REQUIRE_FALSE(throttled.accepted);
  REQUIRE(throttled.reject_reason == RejectReason::MessageRateLimit);
  const RiskAuditEntry& entry = risk.audit().entries().back();
  REQUIRE(entry.check_name == "message_rate_limit");
  REQUIRE(entry.limit_value == 2);
  REQUIRE(entry.observed_value == 3);

  // A different client has its own independent budget.
  REQUIRE(risk.check_new_order(order(4, Side::Buy, 1000, 10, 30, 8), 30).accepted);

  // After the window elapses the original client is allowed again.
  REQUIRE(risk.check_new_order(order(5, Side::Buy, 1000, 10, 200, 7), 200).accepted);
}

TEST_CASE("Self-trade prevention blocks a client crossing its own resting order",
          "[risk][stp]") {
  RiskLimits limits;
  limits.enable_self_trade_prevention = true;
  RiskGateway risk(limits);
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(order(1, Side::Sell, 1001, 10, 10, 7), 10).accepted);

  const auto self_cross = risk.check_new_order(order(2, Side::Buy, 1001, 10, 20, 7), 20);
  REQUIRE_FALSE(self_cross.accepted);
  REQUIRE(self_cross.reject_reason == RejectReason::SelfTradePrevention);
  REQUIRE(risk.audit().entries().back().check_name == "self_trade_prevention");
  REQUIRE(risk.audit().entries().back().observed_value == 1001);

  // A market buy from the same client also self-crosses.
  const auto self_market =
      risk.check_new_order(NewOrderRequest{3, 1, Side::Buy, OrderType::Market, 0, 10, 21, 7}, 21);
  REQUIRE_FALSE(self_market.accepted);
  REQUIRE(self_market.reject_reason == RejectReason::SelfTradePrevention);

  // A different client crossing the same price is not a self-trade.
  REQUIRE(risk.check_new_order(order(4, Side::Buy, 1001, 10, 22, 8), 22).accepted);

  // Once the resting sell is released the original client may cross again.
  risk.release_order(1);
  REQUIRE(risk.check_new_order(order(5, Side::Buy, 1001, 10, 23, 7), 23).accepted);
}

TEST_CASE("Default gateway leaves the new controls disabled", "[risk][compat]") {
  RiskGateway risk; // default limits => phase-6 controls off
  risk.on_market_data(1, 1000, 0);
  for (ClientOrderId id = 1; id <= 5; ++id) {
    const auto result =
        risk.check_new_order(order(id, Side::Buy, 1000, 100, static_cast<TimestampNs>(id), 7),
                             static_cast<TimestampNs>(id));
    REQUIRE(result.accepted);
  }
  REQUIRE(risk.working_quantity(1) == 0); // not tracked when disabled
}

TEST_CASE("Risk control audit checksum is deterministic and recomputable", "[risk][audit]") {
  RiskGateway first(all_controls_limits());
  RiskGateway second(all_controls_limits());
  populate_audit(first);
  populate_audit(second);

  REQUIRE(first.audit().size() == 3);
  REQUIRE(first.audit().checksum() == second.audit().checksum());
  REQUIRE(first.audit().checksum() != kFnvOffsetBasis);
  REQUIRE(first.audit().checksum() == checksum_risk_audit(first.audit().entries()));
}

TEST_CASE("Warm self-trade prevention reject path does not allocate", "[alloc][risk]") {
  RiskLimits limits;
  limits.enable_self_trade_prevention = true;
  RiskGateway risk(limits);
  risk.on_market_data(1, 1000, 0);
  REQUIRE(risk.check_new_order(order(1, Side::Sell, 1001, 10, 10, 7), 10).accepted);
  (void)risk.check_new_order(order(2, Side::Buy, 1001, 10, 20, 7), 20);

  reset_allocation_counters();
  const RiskResult reject = risk.check_new_order(order(3, Side::Buy, 1001, 10, 30, 7), 30);
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE_FALSE(reject.accepted);
  REQUIRE(reject.reject_reason == RejectReason::SelfTradePrevention);
  REQUIRE(snapshot.allocations == 0);
}
