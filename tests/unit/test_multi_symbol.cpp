#include "asterion/book/order_book.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event.hpp"
#include "asterion/market_data/multi_symbol_book.hpp"

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
