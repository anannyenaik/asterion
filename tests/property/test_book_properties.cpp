#include "asterion/book/order_book.hpp"
#include "asterion/market_data/event.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/market_data/synthetic_generator.hpp"
#include "asterion/matching/matching_engine.hpp"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <random>
#include <string>
#include <vector>

using namespace asterion;

namespace {

enum class OpKind { Add, Cancel, Replace };

struct Op {
  OpKind kind{OpKind::Add};
  OrderId order_id{0};
  Side side{Side::Buy};
  PriceTicks price{0};
  Quantity quantity{0};
  PriceTicks new_price{0};
  Quantity new_quantity{0};
};

struct ActiveEngineOrder {
  OrderId order_id{kInvalidOrderId};
  Side side{Side::None};
};

std::uint64_t apply_ops(const std::vector<Op>& ops) {
  OrderBook book(1);
  SequenceNumber sequence = 1;
  for (const Op& op : ops) {
    if (op.kind == OpKind::Add) {
      REQUIRE(book.add_order(Order{op.order_id, op.order_id + 10'000, 1, op.side, op.price,
                                   op.quantity, static_cast<TimestampNs>(sequence), sequence}));
    } else if (op.kind == OpKind::Cancel) {
      REQUIRE(book.cancel_order(op.order_id));
    } else {
      REQUIRE(book.replace_order(op.order_id, op.new_price, op.new_quantity,
                                 static_cast<TimestampNs>(sequence), sequence));
    }

    const auto report = book.check_invariants();
    const std::string violation = report.violations.empty() ? "" : report.violations.front();
    INFO(violation);
    REQUIRE(report.ok);
    ++sequence;
  }
  return book.checksum();
}

void apply_reports_to_active_orders(const std::vector<ExecutionReport>& reports,
                                    std::vector<ActiveEngineOrder>& active_orders) {
  for (const ExecutionReport& report : reports) {
    if (report.exchange_order_id == kInvalidOrderId) {
      continue;
    }

    const auto existing =
        std::find_if(active_orders.begin(), active_orders.end(), [&](const auto& active) {
          return active.order_id == report.exchange_order_id;
        });
    const bool is_resting =
        report.remaining_quantity > 0 && report.order_status != OrderStatus::Canceled &&
        report.order_status != OrderStatus::Filled && report.order_status != OrderStatus::Rejected;

    if (is_resting) {
      if (existing == active_orders.end()) {
        active_orders.push_back(ActiveEngineOrder{report.exchange_order_id, report.side});
      } else {
        existing->side = report.side;
      }
    } else if (existing != active_orders.end()) {
      active_orders.erase(existing);
    }
  }
}

std::uint64_t run_randomized_matching_stream(std::uint32_t seed) {
  std::mt19937 rng(seed);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> price_dist(998, 1002);
  std::uniform_int_distribution<int> qty_dist(1, 60);
  std::uniform_int_distribution<int> op_dist(0, 99);

  MatchingEngine engine(1);
  std::vector<ActiveEngineOrder> active_orders;
  ClientOrderId next_client_order_id = 1;

  for (std::size_t i = 0; i < 500; ++i) {
    const int op = op_dist(rng);
    std::vector<ExecutionReport> reports;
    if (active_orders.empty() || op < 55) {
      const Side side = side_dist(rng) == 0 ? Side::Buy : Side::Sell;
      const OrderType type = op >= 45 ? OrderType::Market : OrderType::Limit;
      const PriceTicks price =
          type == OrderType::Market ? 0 : static_cast<PriceTicks>(price_dist(rng));
      const Quantity quantity = static_cast<Quantity>(qty_dist(rng));
      reports = engine.submit_order(NewOrderRequest{
          next_client_order_id++, 1, side, type, price, quantity,
          static_cast<TimestampNs>(i + 1U)});
    } else {
      std::uniform_int_distribution<std::size_t> active_dist(std::size_t{0},
                                                             active_orders.size() - 1U);
      const std::size_t index = active_dist(rng);
      const OrderId order_id = active_orders[index].order_id;
      if (op < 75) {
        reports = engine.cancel_order(
            CancelOrderRequest{next_client_order_id++, order_id, static_cast<TimestampNs>(i + 1U)});
      } else {
        const PriceTicks new_price = static_cast<PriceTicks>(price_dist(rng));
        const Quantity new_quantity = static_cast<Quantity>(qty_dist(rng));
        reports = engine.replace_order(ReplaceOrderRequest{
            next_client_order_id++, order_id, new_price, new_quantity,
            static_cast<TimestampNs>(i + 1U)});
      }
    }

    apply_reports_to_active_orders(reports, active_orders);
    const auto report = engine.book().check_invariants();
    const std::string violation = report.violations.empty() ? "" : report.violations.front();
    INFO(violation);
    REQUIRE(report.ok);
  }

  return engine.book().checksum() ^ engine.reports_checksum();
}

std::string event_stream_fingerprint(const std::vector<MarketDataEvent>& events) {
  std::string fingerprint;
  for (const MarketDataEvent& event : events) {
    fingerprint += market_data_event_to_csv(event);
    fingerprint += '\n';
  }
  return fingerprint;
}

} // namespace

TEST_CASE("Randomized order-book streams preserve invariants and deterministic checksum",
          "[property][book]") {
  std::mt19937 rng(42);
  std::uniform_int_distribution<int> side_dist(0, 1);
  std::uniform_int_distribution<int> price_dist(990, 1010);
  std::uniform_int_distribution<int> qty_dist(1, 500);
  std::uniform_int_distribution<int> op_dist(0, 99);

  std::vector<Op> ops;
  std::vector<OrderId> active;
  OrderId next_order_id = 1;

  for (std::size_t i = 0; i < 400; ++i) {
    const bool should_add = active.empty() || op_dist(rng) < 55;
    if (should_add) {
      const OrderId order_id = next_order_id++;
      active.push_back(order_id);
      ops.push_back(Op{OpKind::Add, order_id, side_dist(rng) == 0 ? Side::Buy : Side::Sell,
                       static_cast<PriceTicks>(price_dist(rng)),
                       static_cast<Quantity>(qty_dist(rng)), 0, 0});
      continue;
    }

    std::uniform_int_distribution<std::size_t> active_dist(std::size_t{0}, active.size() - 1U);
    const std::size_t index = active_dist(rng);
    const OrderId order_id = active[index];
    if (op_dist(rng) < 50) {
      ops.push_back(Op{OpKind::Cancel, order_id});
      active.erase(active.begin() + static_cast<std::ptrdiff_t>(index));
    } else {
      ops.push_back(Op{OpKind::Replace, order_id, Side::None, 0, 0,
                       static_cast<PriceTicks>(price_dist(rng)),
                       static_cast<Quantity>(qty_dist(rng))});
    }
  }

  const std::uint64_t checksum_a = apply_ops(ops);
  const std::uint64_t checksum_b = apply_ops(ops);
  REQUIRE(checksum_a == checksum_b);
}

TEST_CASE("Randomized matching streams preserve invariants across crossing flow",
          "[property][matching]") {
  const std::uint64_t checksum_a = run_randomized_matching_stream(1337);
  const std::uint64_t checksum_b = run_randomized_matching_stream(1337);
  REQUIRE(checksum_a == checksum_b);
}

TEST_CASE("Synthetic replay generator modes are deterministic and replayable",
          "[property][replay]") {
  const std::vector<SyntheticFlowMode> single_symbol_modes{
      SyntheticFlowMode::Balanced, SyntheticFlowMode::HighCancellationRate,
      SyntheticFlowMode::ReplaceHeavy, SyntheticFlowMode::DeepBook,
      SyntheticFlowMode::BurstyFlow, SyntheticFlowMode::LongRunningSameSymbol,
      SyntheticFlowMode::WidePriceRange, SyntheticFlowMode::AdversarialLifecycle};

  for (const SyntheticFlowMode mode : single_symbol_modes) {
    SyntheticGeneratorConfig config;
    config.event_count = 250;
    config.seed = 99;
    config.mode = mode;

    const std::vector<MarketDataEvent> first = generate_synthetic_events(config);
    const std::vector<MarketDataEvent> second = generate_synthetic_events(config);
    REQUIRE(event_stream_fingerprint(first) == event_stream_fingerprint(second));
    REQUIRE(first.size() == config.event_count);

    for (std::size_t i = 0; i < first.size(); ++i) {
      REQUIRE(first[i].sequence_number == config.first_sequence_number +
                                               static_cast<SequenceNumber>(i));
      if (i > 0) {
        REQUIRE(first[i].timestamp_ns >= first[i - 1U].timestamp_ns);
      }
    }

    ReplayEngine replay(config.symbol_id);
    const ReplayResult result = replay.replay_events(first);
    INFO(result.error);
    REQUIRE(result.error.empty());
    REQUIRE(result.sequence_valid);
  }
}

TEST_CASE("Synthetic replay generator can emit deterministic multi-symbol streams",
          "[property][replay]") {
  SyntheticGeneratorConfig config;
  config.event_count = 120;
  config.seed = 123;
  config.mode = SyntheticFlowMode::MultiSymbol;
  config.symbol_count = 3;

  const std::vector<MarketDataEvent> events = generate_synthetic_events(config);
  bool saw_second_symbol = false;
  bool saw_third_symbol = false;
  for (const MarketDataEvent& event : events) {
    saw_second_symbol = saw_second_symbol || event.symbol_id == config.symbol_id + 1U;
    saw_third_symbol = saw_third_symbol || event.symbol_id == config.symbol_id + 2U;
  }

  REQUIRE(saw_second_symbol);
  REQUIRE(saw_third_symbol);
  REQUIRE(event_stream_fingerprint(events) ==
          event_stream_fingerprint(generate_synthetic_events(config)));
}

TEST_CASE("Simulated market-data adapter drains a deterministic generated stream",
          "[property][replay]") {
  SyntheticGeneratorConfig config;
  config.event_count = 64;
  config.seed = 404;
  config.mode = SyntheticFlowMode::BurstyFlow;

  SimulatedMarketDataAdapter adapter(config);
  const std::vector<MarketDataEvent> drained = adapter.drain();
  REQUIRE(event_stream_fingerprint(drained) ==
          event_stream_fingerprint(generate_synthetic_events(config)));
  REQUIRE_FALSE(adapter.next().has_value());
}
