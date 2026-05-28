#include "asterion/book/order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>

#include <benchmark/benchmark.h>

using namespace asterion;

namespace {

Order make_order(OrderId order_id, Side side, PriceTicks price, Quantity quantity) {
  return Order{order_id, order_id + 1'000'000, 1, side, price, quantity,
               static_cast<TimestampNs>(order_id), order_id};
}

void BM_AddOrder(benchmark::State& state) {
  for (auto _ : state) {
    OrderBook book(1);
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1);
      bool added = book.add_order(make_order(order_id, Side::Buy, 1000, 10));
      benchmark::DoNotOptimize(added);
    }
    std::uint64_t checksum = book.checksum();
    benchmark::DoNotOptimize(checksum);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_CancelOrder(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    OrderBook book(1);
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1);
      (void)book.add_order(make_order(order_id, Side::Sell, 1001, 10));
    }
    state.ResumeTiming();
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1);
      bool canceled = book.cancel_order(order_id);
      benchmark::DoNotOptimize(canceled);
    }
    std::uint64_t checksum = book.checksum();
    benchmark::DoNotOptimize(checksum);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_ReplaceOrder(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    OrderBook book(1);
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1);
      (void)book.add_order(make_order(order_id, Side::Buy, 999, 10));
    }
    (void)book.add_order(make_order(900'000, Side::Buy, 1000, 1));
    (void)book.add_order(make_order(900'001, Side::Buy, 1001, 1));
    state.ResumeTiming();
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1);
      const PriceTicks new_price = 1000 + static_cast<PriceTicks>(i % 2);
      bool replaced = book.replace_order(order_id, new_price, 11, static_cast<TimestampNs>(i + 1),
                                         static_cast<SequenceNumber>(i + 1));
      benchmark::DoNotOptimize(replaced);
    }
    std::uint64_t checksum = book.checksum();
    benchmark::DoNotOptimize(checksum);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_MarketCrossOneLevel(benchmark::State& state) {
  for (auto _ : state) {
    state.PauseTiming();
    MatchingEngine engine(1);
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      (void)engine.submit_order(NewOrderRequest{static_cast<ClientOrderId>(i + 1), 1, Side::Sell,
                                                OrderType::Limit, 1001, 10,
                                                static_cast<TimestampNs>(i + 1)});
    }
    state.ResumeTiming();
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      const auto reports = engine.submit_order(NewOrderRequest{
          static_cast<ClientOrderId>(1'000'000 + i), 1, Side::Buy, OrderType::Market, 0, 10,
          static_cast<TimestampNs>(1'000'000 + i)});
      std::size_t report_count = reports.size();
      benchmark::DoNotOptimize(report_count);
    }
    std::uint64_t checksum = engine.reports_checksum();
    benchmark::DoNotOptimize(checksum);
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_MarketCrossMultipleLevels(benchmark::State& state) {
  for (auto _ : state) {
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      MatchingEngine engine(1);
      const ClientOrderId base = static_cast<ClientOrderId>(i * 10 + 1);
      (void)engine.submit_order(
          NewOrderRequest{base, 1, Side::Sell, OrderType::Limit, 1001, 10, 1});
      (void)engine.submit_order(
          NewOrderRequest{base + 1U, 1, Side::Sell, OrderType::Limit, 1002, 10, 2});
      (void)engine.submit_order(
          NewOrderRequest{base + 2U, 1, Side::Sell, OrderType::Limit, 1003, 10, 3});
      const auto reports = engine.submit_order(
          NewOrderRequest{base + 3U, 1, Side::Buy, OrderType::Market, 0, 30, 4});
      std::size_t report_count = reports.size();
      std::uint64_t checksum = engine.reports_checksum();
      benchmark::DoNotOptimize(report_count);
      benchmark::DoNotOptimize(checksum);
    }
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_L2SnapshotGeneration(benchmark::State& state) {
  OrderBook book(1);
  for (std::int64_t i = 0; i < 100; ++i) {
    const PriceTicks offset = static_cast<PriceTicks>(i);
    (void)book.add_order(make_order(static_cast<OrderId>(i + 1), Side::Buy, 1000 - offset, 10));
    (void)book.add_order(
        make_order(static_cast<OrderId>(i + 10'001), Side::Sell, 1001 + offset, 10));
  }

  for (auto _ : state) {
    const L2View view = book.l2_view(static_cast<std::size_t>(state.range(0)));
    std::size_t bid_count = view.bids.size();
    std::size_t ask_count = view.asks.size();
    benchmark::DoNotOptimize(bid_count);
    benchmark::DoNotOptimize(ask_count);
  }
}

void BM_ReplaySampleEvents(benchmark::State& state) {
  const std::filesystem::path path =
      std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples" / "sample_replay.csv";
  for (auto _ : state) {
    ReplayEngine replay(1);
    const ReplayResult result = replay.replay_file(path);
    std::uint64_t checksum = result.final_book_checksum;
    benchmark::DoNotOptimize(checksum);
  }
}

void BM_RiskCheckOnly(benchmark::State& state) {
  for (auto _ : state) {
    RiskGateway risk(RiskLimits{1'000, 2'000'000, 100'000, 100'000'000, 100, 1'000'000});
    risk.on_market_data(1, 1000, 100);
    for (std::int64_t i = 0; i < state.range(0); ++i) {
      const auto result = risk.check_new_order(
          NewOrderRequest{static_cast<ClientOrderId>(i + 1), 1, Side::Buy, OrderType::Limit,
                          1000, 1, static_cast<TimestampNs>(101 + i)},
          static_cast<TimestampNs>(101 + i));
      bool accepted = result.accepted;
      benchmark::DoNotOptimize(accepted);
    }
  }
  state.SetItemsProcessed(state.iterations() * state.range(0));
}

void BM_LinearInferenceOnly(benchmark::State& state) {
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  const std::array<double, 4> features{2.0, 1000.0, 0.35, 400.0};
  for (auto _ : state) {
    double score = model.score(features);
    benchmark::DoNotOptimize(score);
  }
}

} // namespace

BENCHMARK(BM_AddOrder)->Arg(1'000);
BENCHMARK(BM_CancelOrder)->Arg(1'000);
BENCHMARK(BM_ReplaceOrder)->Arg(1'000);
BENCHMARK(BM_MarketCrossOneLevel)->Arg(1'000);
BENCHMARK(BM_MarketCrossMultipleLevels)->Arg(100);
BENCHMARK(BM_L2SnapshotGeneration)->Arg(25);
BENCHMARK(BM_ReplaySampleEvents);
BENCHMARK(BM_RiskCheckOnly)->Arg(1'000);
BENCHMARK(BM_LinearInferenceOnly);

BENCHMARK_MAIN();
