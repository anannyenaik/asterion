#include "asterion/core/allocation_tracker.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/matching/execution_report.hpp"
#include "asterion/risk/risk_audit.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>

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

ExecutionReport report(ClientOrderId client_order_id, Side side, OrderStatus status,
                       ExecType exec_type, Quantity filled, Quantity remaining,
                       PriceTicks resting_price, TimestampNs now_ns,
                       RejectReason reject_reason = RejectReason::None) {
  ExecutionReport execution_report;
  execution_report.client_order_id = client_order_id;
  execution_report.exchange_order_id = client_order_id + 10'000;
  execution_report.symbol_id = 1;
  execution_report.side = side;
  execution_report.order_status = status;
  execution_report.exec_type = exec_type;
  execution_report.filled_quantity = filled;
  execution_report.remaining_quantity = remaining;
  execution_report.last_fill_quantity = exec_type == ExecType::Trade ? filled : 0;
  execution_report.last_fill_price_ticks = exec_type == ExecType::Trade ? resting_price : 0;
  execution_report.average_price_ticks = filled > 0 ? resting_price : 0;
  execution_report.resting_price_ticks = remaining > 0 ? resting_price : 0;
  execution_report.timestamp_ns = now_ns;
  execution_report.reject_reason = reject_reason;
  return execution_report;
}

std::filesystem::path temp_audit_path() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("asterion_risk_audit_" + std::to_string(stamp) + ".jsonl");
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

TEST_CASE("Sliding-window message-rate limit expires individual messages", "[risk][rate]") {
  RiskLimits limits;
  limits.max_messages_per_window = 2;
  limits.rate_window_ns = 100;
  limits.rate_limit_mode = RateLimitMode::SlidingWindow;
  RiskGateway risk(limits);
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(order(1, Side::Buy, 1000, 10, 0, 7), 0).accepted);
  REQUIRE(risk.check_new_order(order(2, Side::Buy, 1000, 10, 90, 7), 90).accepted);

  const auto throttled = risk.check_new_order(order(3, Side::Buy, 1000, 10, 99, 7), 99);
  REQUIRE_FALSE(throttled.accepted);
  REQUIRE(throttled.reject_reason == RejectReason::MessageRateLimit);
  REQUIRE(risk.audit().entries().back().check_name == "message_rate_limit_sliding");
  REQUIRE(risk.audit().entries().back().observed_value == 3);

  REQUIRE(risk.check_new_order(order(4, Side::Buy, 1000, 10, 200, 7), 200).accepted);
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

TEST_CASE("Execution reports automatically release working exposure", "[risk][working]") {
  RiskLimits limits;
  limits.max_open_order_quantity = 150;
  limits.enable_self_trade_prevention = true;
  RiskGateway risk(limits);
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(order(1, Side::Buy, 1000, 100, 10, 7), 10).accepted);
  REQUIRE(risk.working_quantity(1) == 100);

  risk.on_execution_report(
      report(1, Side::Buy, OrderStatus::PartiallyFilled, ExecType::Trade, 60, 40, 1000, 20));
  REQUIRE(risk.working_quantity(1) == 40);
  REQUIRE(risk.check_new_order(order(2, Side::Buy, 1000, 110, 21, 7), 21).accepted);
  REQUIRE(risk.working_quantity(1) == 150);

  risk.on_execution_report(
      report(1, Side::Buy, OrderStatus::Filled, ExecType::Trade, 100, 0, 1000, 22));
  REQUIRE(risk.working_quantity(1) == 110);

  risk.on_execution_report(
      report(2, Side::Buy, OrderStatus::Canceled, ExecType::Canceled, 0, 0, 0, 23));
  REQUIRE(risk.working_quantity(1) == 0);

  risk.on_execution_report(report(999, Side::Buy, OrderStatus::Rejected, ExecType::Rejected, 0, 0,
                                  0, 24, RejectReason::UnknownOrder));
  REQUIRE(risk.working_quantity(1) == 0);
  REQUIRE(risk.audit().checksum() == checksum_risk_audit(risk.audit().entries()));
}

TEST_CASE("Replace execution reports resize exposure and self-trade books",
          "[risk][working][replace]") {
  RiskLimits limits;
  limits.enable_self_trade_prevention = true;
  RiskGateway risk(limits);
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(order(1, Side::Sell, 1001, 100, 10, 7), 10).accepted);
  REQUIRE_FALSE(risk.check_new_order(order(2, Side::Buy, 1001, 10, 11, 7), 11).accepted);

  risk.on_execution_report(
      report(1, Side::Sell, OrderStatus::Replaced, ExecType::Replaced, 0, 50, 1005, 12));
  REQUIRE(risk.working_quantity(1) == 50);

  const auto after_reprice = risk.check_new_order(order(3, Side::Buy, 1001, 10, 13, 7), 13);
  REQUIRE(after_reprice.accepted);
  risk.release_order(3);

  risk.on_execution_report(
      report(1, Side::Sell, OrderStatus::Replaced, ExecType::Replaced, 0, 120, 1002, 14));
  REQUIRE(risk.working_quantity(1) == 120);
  REQUIRE_FALSE(risk.check_new_order(order(4, Side::Buy, 1002, 10, 15, 7), 15).accepted);
}

TEST_CASE("Kill switch cancels tracked working exposure and blocks new orders",
          "[risk][kill-switch]") {
  RiskLimits limits;
  limits.max_open_order_quantity = 500;
  RiskGateway risk(limits);
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(order(1, Side::Buy, 1000, 100, 10, 7), 10).accepted);
  REQUIRE(risk.check_new_order(order(2, Side::Sell, 1001, 80, 11, 7), 11).accepted);
  REQUIRE(risk.working_quantity(1) == 180);

  risk.enable_kill_switch(20);
  REQUIRE(risk.kill_switch_enabled());
  REQUIRE(risk.working_quantity(1) == 0);
  REQUIRE(risk.exposure_snapshot().working_order_count == 0);

  const RiskAuditEntry& cancel_entry = risk.audit().entries()[2];
  REQUIRE(cancel_entry.check_name == "kill_switch_cancel");
  REQUIRE(cancel_entry.reject_reason == RejectReason::KillSwitch);

  const auto rejected = risk.check_new_order(order(3, Side::Buy, 1000, 10, 21, 7), 21);
  REQUIRE_FALSE(rejected.accepted);
  REQUIRE(rejected.reject_reason == RejectReason::KillSwitch);
}

TEST_CASE("Persistent risk audit log appends JSONL entries with deterministic checksum",
          "[risk][audit]") {
  const std::filesystem::path path = temp_audit_path();
  RiskGateway risk;
  REQUIRE(risk.open_audit_log(path, RiskAuditLogFormat::Jsonl));
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(order(1, Side::Buy, 1000, 10, 10, 7), 10).accepted);
  (void)risk.check_new_order(order(1, Side::Buy, 1000, 10, 11, 7), 11);
  risk.close_audit_log();

  std::ifstream input(path);
  std::string first;
  std::string second;
  REQUIRE(std::getline(input, first));
  REQUIRE(std::getline(input, second));
  REQUIRE(first.find("\"check_name\":\"accepted\"") != std::string::npos);
  REQUIRE(second.find("\"check_name\":\"duplicate_client_order_id\"") != std::string::npos);
  REQUIRE(second.find("\"checksum\":" + std::to_string(risk.audit().checksum())) !=
          std::string::npos);
  std::filesystem::remove(path);
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
