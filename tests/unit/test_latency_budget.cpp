#include "asterion/telemetry/latency_budget.hpp"

#include <catch2/catch_test_macros.hpp>

#include <string>

using namespace asterion;

TEST_CASE("Latency budget accounting reports utilization and breaches", "[latency][budget]") {
  LatencyBudgetConfig config;
  config.replay_ns = 1'000;
  config.matching_ns = 500;
  // book_update_ns is left at 0, meaning "no budget configured".
  LatencyBudgetAccountant accountant(config);

  accountant.record(LatencyStage::Replay, 200);
  accountant.record(LatencyStage::Replay, 800);
  accountant.record(LatencyStage::Matching, 600);
  accountant.record(LatencyStage::BookUpdate, 5'000);

  const StageBudgetReport replay = accountant.report_for(LatencyStage::Replay);
  REQUIRE(replay.has_budget);
  REQUIRE(replay.sample_count == 2);
  REQUIRE(replay.worst_observed_ns == 800);
  REQUIRE(replay.total_observed_ns == 1'000);
  REQUIRE_FALSE(replay.exceeded);
  REQUIRE(replay.worst_utilization_ppm == 800'000);

  const StageBudgetReport matching = accountant.report_for(LatencyStage::Matching);
  REQUIRE(matching.exceeded);
  REQUIRE(matching.worst_utilization_ppm == 1'200'000);

  const StageBudgetReport book = accountant.report_for(LatencyStage::BookUpdate);
  REQUIRE_FALSE(book.has_budget);
  REQUIRE_FALSE(book.exceeded);
  REQUIRE(book.worst_observed_ns == 5'000);

  REQUIRE(accountant.exceeded_count() == 1);
  const auto exceeded = accountant.exceeded_stages();
  REQUIRE(exceeded.size() == 1);
  REQUIRE(exceeded.front() == LatencyStage::Matching);

  const auto worst = accountant.worst_offender();
  REQUIRE(worst.has_value());
  REQUIRE(*worst == LatencyStage::Matching);
}

TEST_CASE("Latency budget config checksum is stable and ignores timings", "[latency][budget]") {
  LatencyBudgetConfig config;
  config.total_ns = 12'345;

  LatencyBudgetAccountant first(config);
  LatencyBudgetAccountant second(config);
  first.record(LatencyStage::Total, 1);
  second.record(LatencyStage::Total, 999'999);
  REQUIRE(first.config_checksum() == second.config_checksum());

  LatencyBudgetConfig changed = config;
  changed.total_ns = 54'321;
  LatencyBudgetAccountant third(changed);
  REQUIRE(third.config_checksum() != first.config_checksum());
}

TEST_CASE("Latency stage names round-trip through parsing", "[latency][budget]") {
  const LatencyStage stages[] = {LatencyStage::Replay,   LatencyStage::BookUpdate,
                                 LatencyStage::Matching, LatencyStage::Risk,
                                 LatencyStage::Strategy, LatencyStage::Inference,
                                 LatencyStage::Total};
  for (const LatencyStage stage : stages) {
    const auto parsed = parse_latency_stage(to_string(stage));
    REQUIRE(parsed.has_value());
    REQUIRE(*parsed == stage);
  }
  REQUIRE_FALSE(parse_latency_stage("not-a-stage").has_value());
}

TEST_CASE("Latency budget JSON exposes stages and summary", "[latency][budget]") {
  LatencyBudgetConfig config;
  config.risk_ns = 100;
  LatencyBudgetAccountant accountant(config);
  accountant.record(LatencyStage::Risk, 250);

  const std::string json = latency_budget_json(accountant);
  REQUIRE(json.find("\"schema_version\": 1") != std::string::npos);
  REQUIRE(json.find("\"stage\": \"risk\"") != std::string::npos);
  REQUIRE(json.find("\"exceeded_count\": 1") != std::string::npos);
  REQUIRE(json.find("\"worst_offender\": \"risk\"") != std::string::npos);
}

TEST_CASE("Latency budget reset clears observations", "[latency][budget]") {
  LatencyBudgetAccountant accountant;
  accountant.record(LatencyStage::Strategy, 42);
  REQUIRE(accountant.report_for(LatencyStage::Strategy).sample_count == 1);
  accountant.reset();
  REQUIRE(accountant.report_for(LatencyStage::Strategy).sample_count == 0);
  REQUIRE(accountant.report_for(LatencyStage::Strategy).worst_observed_ns == 0);
}
