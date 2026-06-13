#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/inference/torchscript_model.hpp"
#include "asterion/strategy/imbalance_strategy.hpp"
#include "asterion/strategy/market_maker.hpp"
#include "asterion/telemetry/latency_histogram.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>

using namespace asterion;

TEST_CASE("Latency histogram reports deterministic percentiles", "[telemetry]") {
  LatencyHistogram histogram;
  histogram.record(10);
  histogram.record(30);
  histogram.record(20);
  histogram.record(40);

  const LatencySummary summary = histogram.summary();
  REQUIRE(summary.count == 4);
  REQUIRE(summary.min_ns == 10);
  REQUIRE(summary.max_ns == 40);
  REQUIRE(summary.p50_ns == 20);
  REQUIRE(summary.p90_ns == 40);
  REQUIRE(summary.p99_ns == 40);
  REQUIRE(summary.p999_ns == 40);
}

TEST_CASE("Linear model scores features deterministically inside the event path", "[inference]") {
  L2View view;
  view.symbol_id = 1;
  view.bids.push_back(L2Level{999, 300});
  view.asks.push_back(L2Level{1001, 100});

  FeatureExtractor extractor;
  const std::vector<double> features = extractor.extract(view);
  LinearModel model({0.5, 0.0, 2.0, 0.001}, 1.0);

  REQUIRE(model.score(features) == Catch::Approx(3.4));
}

TEST_CASE("Feature extraction is versioned and can read directly from book state",
          "[inference]") {
  OrderBook book(7);
  REQUIRE(book.add_order(Order{1, 11, 7, Side::Buy, 999, 300, 1, 1}));
  REQUIRE(book.add_order(Order{2, 12, 7, Side::Sell, 1001, 100, 2, 2}));

  FeatureExtractor extractor;
  const std::vector<double> from_l2 = extractor.extract(book.l2_view(1));
  const std::vector<double> from_book = extractor.extract_from_book(book);
  const FeatureVector versioned = extractor.extract_versioned_from_book(book);

  REQUIRE(extractor.feature_version() == 1);
  REQUIRE(extractor.feature_names().size() == 4);
  REQUIRE(from_book == from_l2);
  REQUIRE(versioned.version == extractor.feature_version());
  REQUIRE(versioned.values == from_l2);
  REQUIRE(versioned.names == extractor.feature_names());
  REQUIRE(from_book[0] == Catch::Approx(2.0));
  REQUIRE(from_book[2] == Catch::Approx(0.5));
}

TEST_CASE("Caller-owned feature buffer matches vector extraction deterministically",
          "[inference][features]") {
  L2View view;
  view.symbol_id = 1;
  view.bids.push_back(L2Level{999, 300});
  view.bids.push_back(L2Level{998, 50});
  view.asks.push_back(L2Level{1001, 100});
  view.asks.push_back(L2Level{1002, 25});

  FeatureExtractor extractor;
  std::array<double, kL2FeatureCount> storage{};
  FeatureBuffer buffer{storage};

  const FeatureExtractionStatus first_status = extractor.extract_into(view, buffer);
  REQUIRE(first_status == FeatureExtractionStatus::Ok);
  REQUIRE(to_string(first_status) == "ok");
  REQUIRE(buffer.size == extractor.feature_count());
  REQUIRE(buffer.size == kL2FeatureCount);

  const std::vector<double> vector_features = extractor.extract(view);
  const std::vector<double> buffer_features(buffer.used().begin(), buffer.used().end());
  REQUIRE(buffer_features == vector_features);
  REQUIRE(buffer_features == std::vector<double>{2.0, 1000.0, 0.5, 400.0});

  std::array<double, kL2FeatureCount> second_storage{};
  FeatureBuffer second_buffer{second_storage};
  const FeatureExtractionStatus second_status = extractor.extract_into(view, second_buffer);
  REQUIRE(second_status == FeatureExtractionStatus::Ok);
  REQUIRE(std::vector<double>(second_buffer.used().begin(), second_buffer.used().end()) ==
          buffer_features);
}

TEST_CASE("Feature buffer capacity checks do not overwrite caller storage",
          "[inference][features]") {
  L2View view;
  view.bids.push_back(L2Level{999, 300});
  view.asks.push_back(L2Level{1001, 100});

  FeatureExtractor extractor;
  std::array<double, kL2FeatureCount - 1U> storage{42.0, 43.0, 44.0};
  FeatureBuffer buffer{storage, 123U};

  const FeatureExtractionStatus status = extractor.extract_into(view, buffer);

  REQUIRE(status == FeatureExtractionStatus::InsufficientCapacity);
  REQUIRE(to_string(status) == "insufficient_capacity");
  REQUIRE(buffer.size == 0);
  REQUIRE(storage[0] == Catch::Approx(42.0));
  REQUIRE(storage[1] == Catch::Approx(43.0));
  REQUIRE(storage[2] == Catch::Approx(44.0));
}

TEST_CASE("Feature buffer preserves metadata and shallow book semantics",
          "[inference][features]") {
  FeatureExtractor extractor;

  REQUIRE(extractor.feature_version() == kL2FeatureVersion);
  REQUIRE(extractor.feature_count() == kL2FeatureCount);
  const auto name_views = extractor.feature_name_views();
  REQUIRE(name_views.size() == kL2FeatureCount);
  REQUIRE(name_views[0] == "spread_ticks");
  REQUIRE(name_views[1] == "mid_price_ticks");
  REQUIRE(name_views[2] == "top_level_imbalance");
  REQUIRE(name_views[3] == "top_level_quantity");

  L2View empty_view;
  std::array<double, kL2FeatureCount> empty_storage{};
  FeatureBuffer empty_buffer{empty_storage};
  REQUIRE(extractor.extract_into(empty_view, empty_buffer) == FeatureExtractionStatus::Ok);
  REQUIRE(std::vector<double>(empty_buffer.used().begin(), empty_buffer.used().end()) ==
          std::vector<double>{0.0, 0.0, 0.0, 0.0});

  L2View shallow_view;
  shallow_view.bids.push_back(L2Level{999, 300});
  std::array<double, kL2FeatureCount> shallow_storage{};
  FeatureBuffer shallow_buffer{shallow_storage};
  REQUIRE(extractor.extract_into(shallow_view, shallow_buffer) == FeatureExtractionStatus::Ok);
  REQUIRE(std::vector<double>(shallow_buffer.used().begin(), shallow_buffer.used().end()) ==
          std::vector<double>{0.0, 0.0, 0.0, 0.0});
}

TEST_CASE("Feature extraction from book can use caller-owned view and feature storage",
          "[inference][features]") {
  OrderBook book(7);
  REQUIRE(book.add_order(Order{1, 11, 7, Side::Buy, 999, 300, 1, 1}));
  REQUIRE(book.add_order(Order{2, 12, 7, Side::Sell, 1001, 100, 2, 2}));

  FeatureExtractor extractor;
  L2View scratch_view;
  scratch_view.reserve(1);
  std::array<double, kL2FeatureCount> storage{};
  FeatureBuffer buffer{storage};

  const FeatureExtractionStatus status =
      extractor.extract_from_book_into(book, buffer, scratch_view);

  REQUIRE(status == FeatureExtractionStatus::Ok);
  REQUIRE(scratch_view.bids.size() == 1);
  REQUIRE(scratch_view.asks.size() == 1);
  REQUIRE(std::vector<double>(buffer.used().begin(), buffer.used().end()) ==
          extractor.extract_from_book(book));
}

TEST_CASE("Measured inference separates model latency from policy decisions", "[inference]") {
  LinearModel model({0.5, 0.0, 2.0, 0.001}, 1.0);
  const std::array<double, 4> features{2.0, 1000.0, 0.5, 400.0};

  InferencePolicy policy;
  policy.timeout_ns = 10;
  policy.max_signal_age_ns = 20;

  const InferencePolicyResult accepted = evaluate_inference_policy(policy, 5, 100, 110);
  REQUIRE_FALSE(accepted.timed_out);
  REQUIRE_FALSE(accepted.late_signal);
  REQUIRE(accepted.accepted);
  REQUIRE(accepted.decision == InferenceDecision::Accept);

  const InferencePolicyResult timed_out = evaluate_inference_policy(policy, 11, 100, 110);
  REQUIRE(timed_out.timed_out);
  REQUIRE_FALSE(timed_out.accepted);
  REQUIRE(timed_out.decision == InferenceDecision::Timeout);

  const InferencePolicyResult late = evaluate_inference_policy(policy, 5, 100, 121);
  REQUIRE(late.late_signal);
  REQUIRE_FALSE(late.accepted);
  REQUIRE(late.decision == InferenceDecision::LateSignal);

  MeasuredInferenceEngine inference(model, InferencePolicy{1'000'000'000, 0, true, true});
  const InferenceResult result = inference.score(features);
  REQUIRE(result.score == Catch::Approx(3.4));
  REQUIRE(result.backend == "linear");
  REQUIRE(result.accepted);
  REQUIRE_FALSE(result.timed_out);
  REQUIRE_FALSE(result.late_signal);
}

TEST_CASE("Late-signal policy accepts within budget and abstains over budget",
          "[inference][policy]") {
  // Injected timings only: signal age = now - signal_ts; latency is supplied directly.
  InferencePolicy policy;
  policy.max_signal_age_ns = 100;
  policy.drop_late_signals = true;

  // Age 80 <= budget 100: accepted.
  const InferencePolicyResult in_budget = evaluate_inference_policy(policy, 0, 1'000, 1'080);
  REQUIRE_FALSE(in_budget.late_signal);
  REQUIRE(in_budget.accepted);

  // Age 150 > budget 100: late, abstain.
  const InferencePolicyResult over_budget = evaluate_inference_policy(policy, 0, 1'000, 1'150);
  REQUIRE(over_budget.late_signal);
  REQUIRE_FALSE(over_budget.accepted);
  REQUIRE(over_budget.decision == InferenceDecision::LateSignal);
}

TEST_CASE("Policy gate disables the model after repeated late signals when configured",
          "[inference][policy][gate]") {
  InferencePolicy policy;
  policy.max_signal_age_ns = 100;
  policy.drop_late_signals = true;
  policy.disable_on_repeated_late_signals = true;
  policy.max_consecutive_late_signals = 3;

  InferencePolicyGate gate(policy);
  REQUIRE_FALSE(gate.model_disabled());

  // Two late signals: late and abstaining, but not yet disabled.
  for (int i = 0; i < 2; ++i) {
    const InferencePolicyResult r = gate.observe(0, 1'000, 1'500);
    REQUIRE(r.late_signal);
    REQUIRE_FALSE(r.accepted);
    REQUIRE_FALSE(r.model_disabled);
  }
  REQUIRE(gate.consecutive_late_signals() == 2);

  // Third consecutive late signal crosses the threshold and latches disabled.
  const InferencePolicyResult third = gate.observe(0, 1'000, 1'500);
  REQUIRE(third.late_signal);
  REQUIRE(third.model_disabled);
  REQUIRE_FALSE(third.accepted);
  REQUIRE(gate.model_disabled());

  // Once disabled it stays disabled and abstains even for an on-time signal.
  const InferencePolicyResult on_time = gate.observe(0, 1'000, 1'050);
  REQUIRE_FALSE(on_time.late_signal);
  REQUIRE(on_time.model_disabled);
  REQUIRE_FALSE(on_time.accepted);

  // Reset clears the latch and the counter.
  gate.reset();
  REQUIRE_FALSE(gate.model_disabled());
  REQUIRE(gate.consecutive_late_signals() == 0);
  const InferencePolicyResult after_reset = gate.observe(0, 1'000, 1'050);
  REQUIRE(after_reset.accepted);
  REQUIRE_FALSE(after_reset.model_disabled);
}

TEST_CASE("Policy gate resets the late counter on an on-time signal", "[inference][policy][gate]") {
  InferencePolicy policy;
  policy.max_signal_age_ns = 100;
  policy.drop_late_signals = true;
  policy.disable_on_repeated_late_signals = true;
  policy.max_consecutive_late_signals = 3;

  InferencePolicyGate gate(policy);
  (void)gate.observe(0, 1'000, 1'500); // late -> 1
  (void)gate.observe(0, 1'000, 1'500); // late -> 2
  REQUIRE(gate.consecutive_late_signals() == 2);

  (void)gate.observe(0, 1'000, 1'050); // on-time resets to 0
  REQUIRE(gate.consecutive_late_signals() == 0);
  REQUIRE_FALSE(gate.model_disabled());

  // Two more late signals are not enough to disable now that the run was broken.
  (void)gate.observe(0, 1'000, 1'500);
  const InferencePolicyResult r = gate.observe(0, 1'000, 1'500);
  REQUIRE_FALSE(r.model_disabled);
  REQUIRE(gate.consecutive_late_signals() == 2);
}

TEST_CASE("Policy gate never disables when the feature is not configured",
          "[inference][policy][gate]") {
  InferencePolicy policy;
  policy.max_signal_age_ns = 100;
  policy.drop_late_signals = true;
  // disable_on_repeated_late_signals stays false (default).

  InferencePolicyGate gate(policy);
  for (int i = 0; i < 10; ++i) {
    const InferencePolicyResult r = gate.observe(0, 1'000, 1'500);
    REQUIRE(r.late_signal);
    REQUIRE_FALSE(r.model_disabled);
  }
  REQUIRE_FALSE(gate.model_disabled());
}

TEST_CASE("TorchScript backend reports its placeholder status until LibTorch is linked",
          "[inference]") {
  TorchScriptModel model("model.ts");
  REQUIRE_FALSE(model.available());
  REQUIRE(model.backend_name() == "torchscript-placeholder");
  REQUIRE_FALSE(model.load_error().empty());
}

TEST_CASE("Example strategies produce bounded deterministic decisions", "[strategy]") {
  L2View view;
  view.symbol_id = 1;
  view.bids.push_back(L2Level{999, 300});
  view.asks.push_back(L2Level{1003, 100});

  MarketMaker maker(5);
  const auto quotes = maker.on_l2_update(view);
  REQUIRE(quotes.size() == 2);
  REQUIRE(quotes[0].side == Side::Buy);
  REQUIRE(quotes[0].price_ticks == 1000);
  REQUIRE(quotes[1].side == Side::Sell);
  REQUIRE(quotes[1].price_ticks == 1002);

  ImbalanceStrategy imbalance(0.70, 7);
  const auto decisions = imbalance.on_l2_update(view);
  REQUIRE(decisions.size() == 1);
  REQUIRE(decisions.front().side == Side::Buy);
}
