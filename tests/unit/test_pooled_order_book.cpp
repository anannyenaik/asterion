#include "asterion/book/order_book.hpp"
#include "asterion/book/pooled_order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

using namespace asterion;

namespace {

Order make_order(OrderId order_id, Side side, PriceTicks price, Quantity quantity,
                 SequenceNumber sequence = 1) {
  return Order{order_id, order_id + 10'000, 1, side, price, quantity,
               static_cast<TimestampNs>(sequence), sequence};
}

void require_same_l2(const L2View& stable, const L2View& pooled) {
  REQUIRE(pooled.symbol_id == stable.symbol_id);
  REQUIRE(pooled.bids.size() == stable.bids.size());
  REQUIRE(pooled.asks.size() == stable.asks.size());
  for (std::size_t i = 0; i < stable.bids.size(); ++i) {
    CAPTURE(i);
    REQUIRE(pooled.bids[i].price_ticks == stable.bids[i].price_ticks);
    REQUIRE(pooled.bids[i].quantity == stable.bids[i].quantity);
  }
  for (std::size_t i = 0; i < stable.asks.size(); ++i) {
    CAPTURE(i);
    REQUIRE(pooled.asks[i].price_ticks == stable.asks[i].price_ticks);
    REQUIRE(pooled.asks[i].quantity == stable.asks[i].quantity);
  }
}

void require_books_equivalent(const OrderBook& stable, const PooledOrderBook& pooled,
                              std::size_t depth = 10) {
  REQUIRE(pooled.symbol_id() == stable.symbol_id());
  REQUIRE(pooled.empty() == stable.empty());
  REQUIRE(pooled.order_count() == stable.order_count());
  REQUIRE(pooled.best_bid() == stable.best_bid());
  REQUIRE(pooled.best_ask() == stable.best_ask());
  REQUIRE(pooled.checksum() == stable.checksum());
  require_same_l2(stable.l2_view(depth), pooled.l2_view(depth));

  const auto stable_report = stable.check_invariants();
  const auto pooled_report = pooled.check_invariants();
  const std::string stable_violation =
      stable_report.violations.empty() ? "" : stable_report.violations.front();
  INFO(stable_violation);
  REQUIRE(stable_report.ok);
  const std::string pooled_violation =
      pooled_report.violations.empty() ? "" : pooled_report.violations.front();
  INFO(pooled_violation);
  REQUIRE(pooled_report.ok);
}

template <typename Fn>
void require_same_result(OrderBook& stable, PooledOrderBook& pooled, Fn&& fn) {
  const bool stable_result = fn(stable);
  const bool pooled_result = fn(pooled);
  REQUIRE(pooled_result == stable_result);
  require_books_equivalent(stable, pooled);
}

template <typename Book>
bool apply_event_to_book(const MarketDataEvent& event, Book& book,
                         std::uint64_t& activity_checksum) {
  switch (event.event_type) {
  case MarketEventType::Add:
    return book.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                event.side, event.price_ticks, event.quantity,
                                event.timestamp_ns, event.sequence_number});
  case MarketEventType::Cancel:
    return event.quantity > 0 ? book.reduce_order(event.order_id, event.quantity)
                              : book.cancel_order(event.order_id);
  case MarketEventType::Replace:
    return book.replace_order(event.order_id, event.price_ticks, event.quantity,
                              event.timestamp_ns, event.sequence_number);
  case MarketEventType::Execute:
    activity_checksum = append_to_checksum(activity_checksum, event);
    return book.reduce_order(event.order_id, event.quantity);
  case MarketEventType::Trade:
    activity_checksum = append_to_checksum(activity_checksum, event);
    return true;
  case MarketEventType::Snapshot:
    if ((event.flags & kSnapshotBeginFlag) != 0U) {
      book.clear();
    }
    if (event.order_id == kInvalidOrderId) {
      return true;
    }
    return book.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                event.side, event.price_ticks, event.quantity,
                                event.timestamp_ns, event.sequence_number});
  case MarketEventType::Heartbeat:
    return true;
  }
  return false;
}

template <typename Book>
struct BookReplaySummary {
  bool ok{true};
  std::uint64_t final_book_checksum{0};
  std::uint64_t activity_checksum{kFnvOffsetBasis};
  L2View final_l2;
};

template <typename Book>
BookReplaySummary<Book> replay_events_with_book(std::span<const MarketDataEvent> events,
                                                std::size_t depth = 5) {
  BookReplaySummary<Book> summary;
  if (events.empty()) {
    Book empty_book(1);
    summary.final_book_checksum = empty_book.checksum();
    return summary;
  }

  Book book(events.front().symbol_id);
  book.reserve_order_capacity(events.size());
  summary.final_l2.reserve(depth);

  for (const MarketDataEvent& event : events) {
    if (!apply_event_to_book(event, book, summary.activity_checksum)) {
      summary.ok = false;
      break;
    }
    book.fill_l2_view(depth, summary.final_l2);
  }
  summary.final_book_checksum = book.checksum();
  return summary;
}

template <typename Book>
std::uint64_t replay_book_once(std::span<const MarketDataEvent> events, Book& book,
                               L2View& view) {
  book.clear();
  book.reserve_order_capacity(events.size());
  std::uint64_t activity_checksum = kFnvOffsetBasis;
  std::uint64_t guard = kFnvOffsetBasis;
  for (const MarketDataEvent& event : events) {
    const bool applied = apply_event_to_book(event, book, activity_checksum);
    book.fill_l2_view(5, view);
    guard = checksum_append(guard, applied ? 1U : 0U);
    guard = checksum_append(guard, static_cast<std::uint64_t>(view.bids.size()));
    guard = checksum_append(guard, static_cast<std::uint64_t>(view.asks.size()));
  }
  guard = checksum_append(guard, activity_checksum);
  guard = checksum_append(guard, book.checksum());
  return guard;
}

template <typename Book>
AllocationSnapshot measure_replay_allocations(std::span<const MarketDataEvent> events,
                                              std::uint64_t& guard) {
  Book book(events.front().symbol_id);
  book.reserve_order_capacity(events.size());
  L2View view;
  view.reserve(5);

  for (std::size_t i = 0; i < 3U; ++i) {
    guard ^= replay_book_once(events, book, view);
  }

  reset_allocation_counters();
  for (std::size_t i = 0; i < 5U; ++i) {
    guard ^= replay_book_once(events, book, view);
  }
  return allocation_snapshot();
}

std::vector<MarketDataEvent> load_events(const std::filesystem::path& path) {
  EventLogReadResult log = read_event_log(path, EventLogFormat::Auto);
  INFO(log.error);
  REQUIRE(log.error.empty());
  REQUIRE_FALSE(log.events.empty());
  return log.events;
}

} // namespace

TEST_CASE("Pooled order book matches stable book for L3 operations",
          "[book][pooled]") {
  OrderBook stable(1);
  PooledOrderBook pooled(1);
  stable.reserve_order_capacity(8);
  pooled.reserve_order_capacity(8);

  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(1, Side::Buy, 1000, 100, 1));
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(2, Side::Buy, 1000, 50, 2));
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(3, Side::Sell, 1002, 40, 3));
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(4, Side::Sell, 1001, 70, 4));
  });

  REQUIRE(pooled.best_order(Side::Buy)->order_id == stable.best_order(Side::Buy)->order_id);
  REQUIRE(pooled.best_order(Side::Buy)->order_id == 1);
  REQUIRE(pooled.best_order(Side::Sell)->order_id == 4);

  require_same_result(stable, pooled, [](auto& book) { return book.reduce_order(1, 25); });
  REQUIRE(pooled.find_order(1)->quantity == stable.find_order(1)->quantity);
  REQUIRE(pooled.find_order(1)->quantity == 75);

  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(1, 999, 60, 5, 5);
  });
  REQUIRE(pooled.best_order(Side::Buy)->order_id == stable.best_order(Side::Buy)->order_id);
  REQUIRE(pooled.best_order(Side::Buy)->order_id == 2);

  require_same_result(stable, pooled, [](auto& book) { return book.reduce_order(4, 70); });
  REQUIRE(pooled.find_order(4) == nullptr);
  REQUIRE(pooled.best_order(Side::Sell)->order_id == 3);

  require_same_result(stable, pooled, [](auto& book) { return book.cancel_order(2); });
  REQUIRE(pooled.best_order(Side::Buy)->order_id == 1);
}

TEST_CASE("Pooled order book rejects the same malformed operations",
          "[book][pooled][adversarial]") {
  OrderBook stable(1);
  PooledOrderBook pooled(1);
  stable.reserve_order_capacity(4);
  pooled.reserve_order_capacity(4);

  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(1, Side::Buy, 1000, 0));
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(2, Side::Sell, 0, 10));
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(3, Side::Buy, 999, 10));
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(3, Side::Buy, 998, 10));
  });
  require_same_result(stable, pooled, [](auto& book) { return book.cancel_order(99); });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(99, 1000, 10, 10, 10);
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(3, 0, 10, 11, 11);
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(3, 1000, 0, 12, 12);
  });
}

TEST_CASE("Pooled order book replay checksums match existing sample logs",
          "[book][pooled][replay]") {
  const std::array<std::filesystem::path, 4> paths{
      std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples" / "sample_replay.csv",
      std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples" / "sample_replay.bin",
      std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples" /
          "sample_hot_path_replay.bin",
      std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples" /
          "binance_depth_sample.normalised.bin",
  };

  for (const auto& path : paths) {
    CAPTURE(path.string());
    const std::vector<MarketDataEvent> events = load_events(path);
    const auto stable = replay_events_with_book<OrderBook>(events);
    const auto pooled = replay_events_with_book<PooledOrderBook>(events);

    REQUIRE(stable.ok);
    REQUIRE(pooled.ok);
    REQUIRE(pooled.final_book_checksum == stable.final_book_checksum);
    REQUIRE(pooled.activity_checksum == stable.activity_checksum);
    require_same_l2(stable.final_l2, pooled.final_l2);

    ReplayEngine replay(events.front().symbol_id);
    const ReplayResult replay_result = replay.replay_events(events);
    INFO(replay_result.error);
    REQUIRE(replay_result.error.empty());
    REQUIRE(replay_result.final_book_checksum == pooled.final_book_checksum);
    REQUIRE(replay_result.execution_report_checksum == pooled.activity_checksum);
  }
}

TEST_CASE("Pooled order book is allocation-free after explicit warm-up",
          "[book][pooled][alloc]") {
  PooledOrderBook book(1);
  book.reserve_order_capacity(8);
  REQUIRE(book.add_order(make_order(1, Side::Buy, 1000, 10)));
  REQUIRE(book.add_order(make_order(2, Side::Sell, 1001, 10)));
  REQUIRE(book.replace_order(1, 999, 9, 3, 3));
  book.clear();

  reset_allocation_counters();
  REQUIRE(book.add_order(make_order(1, Side::Buy, 1000, 10)));
  REQUIRE(book.add_order(make_order(2, Side::Sell, 1001, 10)));
  REQUIRE(book.replace_order(1, 999, 9, 3, 3));
  REQUIRE(book.reduce_order(1, 4));
  REQUIRE(book.reduce_order(1, 5));
  REQUIRE(book.cancel_order(2));
  const AllocationSnapshot snapshot = allocation_snapshot();

  REQUIRE(snapshot.allocations == 0);
}

TEST_CASE("Pooled order book reduces warmed replay allocations versus stable book",
          "[book][pooled][alloc]") {
  const auto path = std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples" /
                    "sample_hot_path_replay.bin";
  const std::vector<MarketDataEvent> events = load_events(path);
  std::uint64_t standard_guard = 0;
  std::uint64_t pooled_guard = 0;

  const AllocationSnapshot standard =
      measure_replay_allocations<OrderBook>(events, standard_guard);
  const AllocationSnapshot pooled =
      measure_replay_allocations<PooledOrderBook>(events, pooled_guard);

  REQUIRE(standard_guard == pooled_guard);
  REQUIRE(standard.allocations > 0);
  REQUIRE(pooled.allocations < standard.allocations);
  REQUIRE(pooled.allocations == 0);
}
