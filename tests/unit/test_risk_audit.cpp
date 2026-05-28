#include "asterion/core/checksum.hpp"
#include "asterion/risk/risk_audit.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>

using namespace asterion;

namespace {

NewOrderRequest limit_buy(ClientOrderId id, PriceTicks price, Quantity quantity,
                          TimestampNs now_ns) {
  return NewOrderRequest{id, 1, Side::Buy, OrderType::Limit, price, quantity, now_ns};
}

} // namespace

TEST_CASE("Risk audit records duplicate client order id rejections", "[risk][audit]") {
  RiskGateway risk(RiskLimits{1'000, 2'000'000, 5'000, 10'000'000, 50, 1'000});
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 100);

  REQUIRE(risk.check_new_order(limit_buy(10, 1001, 100, 101), 101).accepted);
  const auto duplicate = risk.check_new_order(limit_buy(10, 1001, 100, 102), 102);
  REQUIRE_FALSE(duplicate.accepted);
  REQUIRE(duplicate.reject_reason == RejectReason::DuplicateClientOrderId);

  const auto& entries = risk.audit().entries();
  REQUIRE(entries.size() == 2);
  REQUIRE(entries[0].check_name == "accepted");
  REQUIRE(entries[0].accepted);
  REQUIRE(entries[1].check_name == "duplicate_client_order_id");
  REQUIRE_FALSE(entries[1].accepted);
  REQUIRE(entries[1].client_order_id == 10);
  REQUIRE(risk.audit().accepted_count() == 1);
  REQUIRE(risk.audit().rejected_count() == 1);
}

TEST_CASE("Risk audit records kill switch rejections", "[risk][audit]") {
  RiskGateway risk;
  risk.set_audit_enabled(true);
  risk.enable_kill_switch();

  const auto result = risk.check_new_order(limit_buy(1, 1000, 10, 5), 5);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.reject_reason == RejectReason::KillSwitch);
  REQUIRE(risk.audit().size() == 1);
  REQUIRE(risk.audit().entries().front().check_name == "kill_switch");
  REQUIRE(risk.audit().entries().front().symbol_id == 1);
}

TEST_CASE("Risk audit records stale market-data rejections with age", "[risk][audit]") {
  RiskGateway risk(RiskLimits{1'000, 2'000'000, 5'000, 10'000'000, 50, 5});
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 100);

  const auto result = risk.check_new_order(limit_buy(1, 1000, 10, 200), 200);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.reject_reason == RejectReason::StaleMarketData);

  const RiskAuditEntry& entry = risk.audit().entries().front();
  REQUIRE(entry.check_name == "stale_market_data");
  REQUIRE(entry.limit_value == 5);
  REQUIRE(entry.observed_value == 100);
}

TEST_CASE("Risk audit records max order quantity rejections", "[risk][audit]") {
  RiskGateway risk(RiskLimits{50, 1'000'000'000, 1'000'000, 1'000'000'000, 1'000'000, 1'000});
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 100);

  const auto result = risk.check_new_order(limit_buy(1, 1000, 100, 101), 101);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.reject_reason == RejectReason::MaxOrderQuantity);

  const RiskAuditEntry& entry = risk.audit().entries().front();
  REQUIRE(entry.check_name == "max_order_quantity");
  REQUIRE(entry.limit_value == 50);
  REQUIRE(entry.observed_value == 100);
}

TEST_CASE("Risk audit records notional limit rejections", "[risk][audit]") {
  RiskGateway risk(RiskLimits{1'000'000, 5'000, 1'000'000, 1'000'000'000, 1'000'000, 1'000});
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 100);

  // notional = 1000 * 10 = 10000 > 5000
  const auto result = risk.check_new_order(limit_buy(1, 1000, 10, 101), 101);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.reject_reason == RejectReason::MaxNotional);

  const RiskAuditEntry& entry = risk.audit().entries().front();
  REQUIRE(entry.check_name == "max_notional");
  REQUIRE(entry.limit_value == 5'000);
  REQUIRE(entry.observed_value == 10'000);
}

TEST_CASE("Risk audit records position limit rejections", "[risk][audit]") {
  RiskGateway risk(
      RiskLimits{1'000'000, 1'000'000'000, 100, 1'000'000'000'000, 1'000'000, 1'000});
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 100);
  risk.set_position(1, 95);

  // projected position = 95 + 10 = 105 > 100
  const auto result = risk.check_new_order(limit_buy(1, 1000, 10, 101), 101);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.reject_reason == RejectReason::MaxPosition);

  const RiskAuditEntry& entry = risk.audit().entries().front();
  REQUIRE(entry.check_name == "max_position");
  REQUIRE(entry.limit_value == 100);
  REQUIRE(entry.observed_value == 105);
}

TEST_CASE("Risk audit checksum is deterministic and matches recomputation", "[risk][audit]") {
  const auto run = []() {
    RiskGateway risk(RiskLimits{1'000, 2'000'000, 5'000, 10'000'000, 50, 1'000});
    risk.set_audit_enabled(true);
    risk.on_market_data(1, 1000, 100);
    (void)risk.check_new_order(limit_buy(10, 1001, 100, 101), 101);
    (void)risk.check_new_order(limit_buy(10, 1001, 100, 102), 102);
    return risk.audit().checksum();
  };

  const std::uint64_t first = run();
  const std::uint64_t second = run();
  REQUIRE(first == second);
  REQUIRE(first != kFnvOffsetBasis);

  RiskGateway risk(RiskLimits{1'000, 2'000'000, 5'000, 10'000'000, 50, 1'000});
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 100);
  (void)risk.check_new_order(limit_buy(10, 1001, 100, 101), 101);
  (void)risk.check_new_order(limit_buy(11, 1001, 100, 102), 102);
  REQUIRE(risk.audit().checksum() == checksum_risk_audit(risk.audit().entries()));
}
