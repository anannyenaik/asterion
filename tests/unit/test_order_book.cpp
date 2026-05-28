#include "asterion/book/order_book.hpp"
#include "asterion/market_data/replay.hpp"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <string>
#include <vector>

using namespace asterion;

namespace {

Order make_order(OrderId order_id, Side side, PriceTicks price, Quantity quantity,
                 SequenceNumber sequence = 1) {
  return Order{order_id, order_id + 10'000, 1, side, price, quantity,
               static_cast<TimestampNs>(sequence), sequence};
}

} // namespace

TEST_CASE("L3 order book preserves FIFO and aggregates L2 levels", "[book]") {
  OrderBook book(1);

  REQUIRE(book.add_order(make_order(1, Side::Sell, 1001, 100, 1)));
  REQUIRE(book.add_order(make_order(2, Side::Sell, 1001, 50, 2)));
  REQUIRE(book.add_order(make_order(3, Side::Buy, 999, 75, 3)));

  REQUIRE(book.best_ask().has_value());
  REQUIRE(*book.best_ask() == 1001);
  REQUIRE(book.best_bid().has_value());
  REQUIRE(*book.best_bid() == 999);
  REQUIRE(book.best_order(Side::Sell)->order_id == 1);
  REQUIRE(book.total_quantity_at(Side::Sell, 1001) == 150);

  const L2View view = book.l2_view(5);
  REQUIRE(view.asks.size() == 1);
  REQUIRE(view.asks.front().quantity == 150);
  REQUIRE(view.bids.front().price_ticks == 999);

  const auto invariants = book.check_invariants();
  const std::string violation = invariants.violations.empty() ? "" : invariants.violations.front();
  INFO(violation);
  REQUIRE(invariants.ok);
}

TEST_CASE("Order book reductions remove empty levels", "[book]") {
  OrderBook book(1);
  REQUIRE(book.add_order(make_order(1, Side::Buy, 1000, 10)));
  REQUIRE(book.reduce_order(1, 4));
  REQUIRE(book.find_order(1)->quantity == 6);
  REQUIRE(book.reduce_order(1, 6));
  REQUIRE(book.find_order(1) == nullptr);
  REQUIRE_FALSE(book.best_bid().has_value());
  REQUIRE(book.check_invariants().ok);
}

TEST_CASE("Replay engine validates sequence and final checksum deterministically", "[replay]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Sell, 1001, 100, 11, 0, 0},
      MarketDataEvent{2, 2, 1, MarketEventType::Add, Side::Sell, 1001, 50, 12, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Execute, Side::Sell, 1001, 120, 11, 77, 0},
  };

  ReplayEngine replay_a(1);
  ReplayEngine replay_b(1);
  const ReplayResult result_a = replay_a.replay_events(events);
  const ReplayResult result_b = replay_b.replay_events(events);

  REQUIRE(result_a.error.empty());
  REQUIRE(result_a.sequence_valid);
  REQUIRE(result_a.events_processed == events.size());
  REQUIRE(result_a.final_book_checksum == result_b.final_book_checksum);
  REQUIRE(result_a.execution_report_checksum == result_b.execution_report_checksum);
}

TEST_CASE("Replay engine can read the sample CSV", "[replay]") {
  const auto path = std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples" /
                    "sample_replay.csv";
  ReplayEngine replay(1);
  const ReplayResult result = replay.replay_file(path);
  INFO(result.error);
  REQUIRE(result.error.empty());
  REQUIRE(result.sequence_valid);
  REQUIRE(result.events_processed > 0);
}
