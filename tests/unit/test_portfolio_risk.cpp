#include "asterion/core/checksum.hpp"
#include "asterion/risk/portfolio_risk.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace asterion;

TEST_CASE("Default portfolio monitor enforces nothing", "[portfolio]") {
  PortfolioRiskMonitor monitor;
  REQUIRE_FALSE(monitor.active());
  monitor.set_mark(1, 1000);
  monitor.set_position(1, 1'000'000, 1000);
  REQUIRE(monitor.evaluate_state().accepted);
  REQUIRE(monitor.evaluate_order(1, Side::Buy, 999'999, 1000).accepted);
}

TEST_CASE("Portfolio gross exposure limit blocks an over-exposed order", "[portfolio][gross]") {
  PortfolioRiskLimits limits;
  limits.max_gross_exposure_ticks = 100'000;
  PortfolioRiskMonitor monitor(limits);
  monitor.set_mark(1, 1000);
  monitor.set_mark(2, 500);
  monitor.set_position(1, 50, 1000); // 50,000
  monitor.set_position(2, 80, 500);  // 40,000 => gross 90,000
  REQUIRE(monitor.gross_exposure_ticks() == 90'000);
  REQUIRE(monitor.evaluate_state().accepted);

  const PortfolioCheckResult result = monitor.evaluate_order(1, Side::Buy, 20, 1000);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.breach == PortfolioBreach::GrossExposure);
  REQUIRE(result.limit_value == 100'000);
  REQUIRE(result.observed_value == 110'000);
}

TEST_CASE("Portfolio net exposure limit blocks a directional build-up", "[portfolio][net]") {
  PortfolioRiskLimits limits;
  limits.max_net_exposure_ticks = 10'000;
  PortfolioRiskMonitor monitor(limits);
  monitor.set_mark(1, 1000);
  monitor.set_mark(2, 1000);
  monitor.set_position(1, 50, 1000);  // +50,000
  monitor.set_position(2, -45, 1000); // -45,000 => net 5,000
  REQUIRE(monitor.net_exposure_ticks() == 5'000);
  REQUIRE(monitor.evaluate_state().accepted);

  const PortfolioCheckResult result = monitor.evaluate_order(1, Side::Buy, 10, 1000);
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.breach == PortfolioBreach::NetExposure);
  REQUIRE(result.observed_value == 15'000);
}

TEST_CASE("Portfolio concentration limit flags a dominant symbol", "[portfolio][concentration]") {
  PortfolioRiskLimits limits;
  limits.max_symbol_concentration_bps = 6'000; // 60%
  PortfolioRiskMonitor monitor(limits);
  monitor.set_mark(1, 1000);
  monitor.set_mark(2, 1000);
  monitor.set_position(1, 70, 1000); // 70,000
  monitor.set_position(2, 30, 1000); // 30,000 => symbol 1 is 70%

  const PortfolioCheckResult result = monitor.evaluate_state();
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.breach == PortfolioBreach::Concentration);
  REQUIRE(result.symbol_id == 1);
  REQUIRE(result.limit_value == 6'000);
  REQUIRE(result.observed_value == 7'000);
}

TEST_CASE("Portfolio loss threshold uses simulated marks", "[portfolio][loss]") {
  PortfolioRiskLimits limits;
  limits.max_loss_ticks = 5'000;
  PortfolioRiskMonitor monitor(limits);
  monitor.set_position(1, 100, 1000); // long 100 @ 1000
  monitor.set_mark(1, 940);           // unrealised = (940-1000)*100 = -6,000

  const PortfolioCheckResult result = monitor.evaluate_state();
  REQUIRE_FALSE(result.accepted);
  REQUIRE(result.breach == PortfolioBreach::Loss);
  REQUIRE(result.limit_value == 5'000);
  REQUIRE(result.observed_value == 6'000);
}

TEST_CASE("Portfolio monitor tracks realised and unrealised PnL through fills",
          "[portfolio][pnl]") {
  PortfolioRiskMonitor monitor;
  monitor.apply_fill(1, Side::Buy, 100, 1000);
  REQUIRE(monitor.position(1) == 100);
  monitor.set_mark(1, 1010);
  REQUIRE(monitor.unrealised_pnl_ticks() == 1'000);

  monitor.apply_fill(1, Side::Sell, 40, 1010); // realise (1010-1000)*40 = 400
  REQUIRE(monitor.realised_pnl_ticks() == 400);
  REQUIRE(monitor.position(1) == 60);
  REQUIRE(monitor.unrealised_pnl_ticks() == 600);
  REQUIRE(monitor.total_pnl_ticks() == 1'000);
}

TEST_CASE("Portfolio monitor handles a position flip", "[portfolio][pnl]") {
  PortfolioRiskMonitor monitor;
  monitor.apply_fill(1, Side::Buy, 50, 1000);  // long 50 @ 1000
  monitor.apply_fill(1, Side::Sell, 80, 1010); // close 50 (realise 500), flip short 30 @ 1010
  REQUIRE(monitor.position(1) == -30);
  REQUIRE(monitor.realised_pnl_ticks() == 500);

  monitor.set_mark(1, 1000); // short profit (1000-1010)*-30 = 300
  REQUIRE(monitor.unrealised_pnl_ticks() == 300);
}

TEST_CASE("Portfolio check records deterministic audit entries", "[portfolio][audit]") {
  const auto run = []() {
    PortfolioRiskLimits limits;
    limits.max_gross_exposure_ticks = 50'000;
    PortfolioRiskMonitor monitor(limits);
    monitor.set_audit_enabled(true);
    monitor.set_mark(1, 1000);
    (void)monitor.check_order(1, Side::Buy, 30, 1000, 5); // 30,000 -> accepted
    (void)monitor.check_order(1, Side::Buy, 100, 1000, 6); // would be 130,000 -> breach
    return monitor.audit().checksum();
  };

  PortfolioRiskLimits limits;
  limits.max_gross_exposure_ticks = 50'000;
  PortfolioRiskMonitor monitor(limits);
  monitor.set_audit_enabled(true);
  monitor.set_mark(1, 1000);
  REQUIRE(monitor.check_order(1, Side::Buy, 30, 1000, 5).accepted);
  const PortfolioCheckResult breach = monitor.check_order(1, Side::Buy, 100, 1000, 6);
  REQUIRE_FALSE(breach.accepted);

  REQUIRE(monitor.audit().size() == 2);
  REQUIRE(monitor.audit().entries().front().check_name == "portfolio_accepted");
  REQUIRE(monitor.audit().entries().back().check_name == "portfolio_gross_exposure");
  REQUIRE(monitor.audit().entries().back().reject_reason ==
          RejectReason::MaxPortfolioGrossExposure);
  REQUIRE(monitor.audit().checksum() == checksum_risk_audit(monitor.audit().entries()));
  REQUIRE(run() == run());
}

TEST_CASE("Portfolio snapshot checksum is deterministic", "[portfolio][snapshot]") {
  const auto build = []() {
    PortfolioRiskLimits limits;
    limits.max_gross_exposure_ticks = 1'000'000;
    PortfolioRiskMonitor monitor(limits);
    monitor.set_mark(1, 1000);
    monitor.set_mark(2, 2000);
    monitor.apply_fill(1, Side::Buy, 100, 1000);
    monitor.apply_fill(2, Side::Sell, 50, 2000);
    return monitor.snapshot();
  };
  const PortfolioSnapshot first = build();
  const PortfolioSnapshot second = build();
  REQUIRE(first.checksum == second.checksum);
  REQUIRE(first.checksum != kFnvOffsetBasis);
  REQUIRE(first.symbol_count == 2);
}
