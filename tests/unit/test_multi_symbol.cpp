#include "asterion/book/order_book.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event.hpp"
#include "asterion/market_data/multi_symbol_book.hpp"
#include "asterion/market_data/replay_aggregate.hpp"
#include "asterion/market_data/synthetic_generator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace asterion;

TEST_CASE("Multi-symbol book set routes an interleaved stream per symbol", "[multi-symbol]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1001, 5, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Add, Side::Buy, 998, 7, 3, 0, 0},
      MarketDataEvent{4, 4, 2, MarketEventType::Execute, Side::Sell, 1001, 2, 2, 77, 0},
  };

  MultiSymbolBookSet set;
  REQUIRE(set.apply_all(events) == events.size());
  REQUIRE(set.symbol_count() == 2);
  REQUIRE(set.contains(1));
  REQUIRE(set.contains(2));

  // Each per-symbol book matches an independently built single-symbol book.
  OrderBook expected_one(1);
  REQUIRE(expected_one.add_order(Order{1, kInvalidClientOrderId, 1, Side::Buy, 999, 10, 1, 1}));
  REQUIRE(expected_one.add_order(Order{3, kInvalidClientOrderId, 1, Side::Buy, 998, 7, 3, 3}));
  REQUIRE(set.find_book(1) != nullptr);
  REQUIRE(set.find_book(1)->checksum() == expected_one.checksum());

  OrderBook expected_two(2);
  REQUIRE(expected_two.add_order(Order{2, kInvalidClientOrderId, 2, Side::Sell, 1001, 5, 2, 2}));
  REQUIRE(expected_two.reduce_order(2, 2));
  REQUIRE(set.find_book(2) != nullptr);
  REQUIRE(set.find_book(2)->checksum() == expected_two.checksum());
}

TEST_CASE("Multi-symbol combined checksum is deterministic", "[multi-symbol]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1001, 5, 2, 0, 0},
  };
  MultiSymbolBookSet first;
  MultiSymbolBookSet second;
  REQUIRE(first.apply_all(events) == events.size());
  REQUIRE(second.apply_all(events) == events.size());
  REQUIRE(first.combined_checksum() == second.combined_checksum());
  REQUIRE(first.combined_checksum() != kFnvOffsetBasis);
}

TEST_CASE("Multi-symbol book set rejects unknown cancels without corrupting state",
          "[multi-symbol]") {
  MultiSymbolBookSet set;
  REQUIRE(set.apply(MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 10, 1, 0, 0}));
  REQUIRE_FALSE(
      set.apply(MarketDataEvent{2, 2, 1, MarketEventType::Cancel, Side::Buy, 999, 0, 999, 0, 0}));
  REQUIRE(set.find_book(1)->order_count() == 1);
}

TEST_CASE("Shared multi-symbol replay matches grouped replay for generated streams",
          "[multi-symbol][replay]") {
  SyntheticGeneratorConfig config;
  config.event_count = 300;
  config.seed = 20260528;
  config.mode = SyntheticFlowMode::MultiSymbol;
  config.symbol_count = 4;

  const std::vector<MarketDataEvent> events = generate_synthetic_events(config);
  const AggregateReplaySummary grouped = replay_by_symbol(events);
  const AggregateReplaySummary shared = replay_shared_by_symbol(events);

  REQUIRE(grouped.error.empty());
  REQUIRE(shared.error.empty());
  REQUIRE(shared.total_events == grouped.total_events);
  REQUIRE(shared.symbol_count == grouped.symbol_count);
  REQUIRE(shared.combined_book_checksum == grouped.combined_book_checksum);
  REQUIRE(shared.aggregate_checksum == grouped.aggregate_checksum);
  REQUIRE(shared.symbols.size() == grouped.symbols.size());

  for (std::size_t i = 0; i < grouped.symbols.size(); ++i) {
    const SymbolReplaySummary& expected = grouped.symbols[i];
    const SymbolReplaySummary& actual = shared.symbols[i];
    REQUIRE(actual.symbol_id == expected.symbol_id);
    REQUIRE(actual.event_count == expected.event_count);
    REQUIRE(actual.first_sequence == expected.first_sequence);
    REQUIRE(actual.last_sequence == expected.last_sequence);
    REQUIRE(actual.first_timestamp_ns == expected.first_timestamp_ns);
    REQUIRE(actual.last_timestamp_ns == expected.last_timestamp_ns);
    REQUIRE(actual.sequence_valid == expected.sequence_valid);
    REQUIRE(actual.event_log_checksum == expected.event_log_checksum);
    REQUIRE(actual.final_book_checksum == expected.final_book_checksum);
    REQUIRE(actual.execution_report_checksum == expected.execution_report_checksum);
    REQUIRE(actual.diagnostics_checksum == expected.diagnostics_checksum);
    REQUIRE(actual.diagnostic_count == expected.diagnostic_count);
    REQUIRE(actual.diagnostic_error_count == expected.diagnostic_error_count);
    REQUIRE(actual.diagnostic_warning_count == expected.diagnostic_warning_count);
  }
}

TEST_CASE("Shared multi-symbol replay preserves strict per-symbol sequence diagnostics",
          "[multi-symbol][replay][diagnostics]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1001, 5, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Cancel, Side::Buy, 999, 0, 1, 0, 0},
      MarketDataEvent{4, 4, 2, MarketEventType::Execute, Side::Sell, 1001, 2, 2, 77, 0},
  };

  AggregateReplayConfig config;
  config.validate_per_symbol_sequences = true;
  const AggregateReplaySummary grouped = replay_by_symbol(events, config);
  const AggregateReplaySummary shared = replay_shared_by_symbol(events, config);

  REQUIRE(shared.aggregate_checksum == grouped.aggregate_checksum);
  REQUIRE(shared.symbols[0].diagnostic_error_count == 1);
  REQUIRE(shared.symbols[0].diagnostics.front().reason.find("sequence gap") !=
          std::string::npos);
  REQUIRE(shared.symbols[1].diagnostic_error_count == 1);
  REQUIRE(shared.symbols[1].diagnostics.front().reason.find("sequence gap") !=
          std::string::npos);
}
