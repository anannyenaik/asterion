#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/strategy/imbalance_strategy.hpp"
#include "asterion/strategy/market_maker.hpp"
#include "asterion/telemetry/latency_histogram.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

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
