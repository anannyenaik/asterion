#include "asterion/book/order_book.hpp"
#include "asterion/book/pooled_order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/market_data/synthetic_generator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <map>
#include <optional>
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

struct BookReplaySummary {
  bool ok{true};
  std::size_t events_processed{0};
  std::uint64_t event_log_checksum{kFnvOffsetBasis};
  std::uint64_t final_book_checksum{0};
  std::uint64_t execution_report_checksum{kFnvOffsetBasis};
  std::uint64_t diagnostics_checksum{kFnvOffsetBasis};
  std::uint64_t guard_checksum{kFnvOffsetBasis};
  std::optional<PriceTicks> best_bid;
  std::optional<PriceTicks> best_ask;
  Quantity bid_depth{0};
  Quantity ask_depth{0};
  L2View final_l2;
};

Quantity total_depth(std::span<const L2Level> levels) {
  Quantity total = 0;
  for (const L2Level& level : levels) {
    total += level.quantity;
  }
  return total;
}

template <typename Book>
BookReplaySummary replay_events_with_book(std::span<const MarketDataEvent> events,
                                          std::size_t depth = 5) {
  BookReplaySummary summary;
  summary.event_log_checksum = checksum_events(events);
  if (events.empty()) {
    Book empty_book(1);
    summary.final_book_checksum = empty_book.checksum();
    return summary;
  }

  Book book(events.front().symbol_id);
  book.reserve_order_capacity(events.size());
  summary.final_l2.reserve(depth);

  for (const MarketDataEvent& event : events) {
    const bool applied = apply_event_to_book(event, book, summary.execution_report_checksum);
    book.fill_l2_view(depth, summary.final_l2);
    summary.guard_checksum = checksum_append(summary.guard_checksum, applied ? 1U : 0U);
    summary.guard_checksum =
        checksum_append(summary.guard_checksum, static_cast<std::uint64_t>(summary.final_l2.bids.size()));
    summary.guard_checksum =
        checksum_append(summary.guard_checksum, static_cast<std::uint64_t>(summary.final_l2.asks.size()));
    if (!applied) {
      summary.ok = false;
      break;
    }
    ++summary.events_processed;
  }
  summary.final_book_checksum = book.checksum();
  summary.best_bid = book.best_bid();
  summary.best_ask = book.best_ask();
  summary.bid_depth = total_depth(summary.final_l2.bids);
  summary.ask_depth = total_depth(summary.final_l2.asks);
  summary.guard_checksum = checksum_append(summary.guard_checksum, summary.event_log_checksum);
  summary.guard_checksum = checksum_append(summary.guard_checksum, summary.execution_report_checksum);
  summary.guard_checksum = checksum_append(summary.guard_checksum, summary.final_book_checksum);

  const auto invariant_report = book.check_invariants();
  if (!invariant_report.ok) {
    summary.ok = false;
  }
  return summary;
}

void require_same_replay_summary(const BookReplaySummary& stable,
                                 const BookReplaySummary& pooled) {
  REQUIRE(pooled.ok == stable.ok);
  REQUIRE(pooled.events_processed == stable.events_processed);
  REQUIRE(pooled.event_log_checksum == stable.event_log_checksum);
  REQUIRE(pooled.final_book_checksum == stable.final_book_checksum);
  REQUIRE(pooled.execution_report_checksum == stable.execution_report_checksum);
  REQUIRE(pooled.diagnostics_checksum == stable.diagnostics_checksum);
  REQUIRE(pooled.guard_checksum == stable.guard_checksum);
  REQUIRE(pooled.best_bid == stable.best_bid);
  REQUIRE(pooled.best_ask == stable.best_ask);
  REQUIRE(pooled.bid_depth == stable.bid_depth);
  REQUIRE(pooled.ask_depth == stable.ask_depth);
  require_same_l2(stable.final_l2, pooled.final_l2);
}

std::map<SymbolId, std::vector<MarketDataEvent>>
group_events_by_symbol(std::span<const MarketDataEvent> events) {
  std::map<SymbolId, std::vector<MarketDataEvent>> grouped;
  for (const MarketDataEvent& event : events) {
    grouped[event.symbol_id].push_back(event);
  }
  return grouped;
}

void require_book_replay_parity(std::span<const MarketDataEvent> events,
                                bool validate_sequence_numbers = true) {
  const std::size_t depth = events.size() + 1U;
  const auto grouped = group_events_by_symbol(events);
  REQUIRE_FALSE(grouped.empty());

  std::uint64_t stable_combined_guard = kFnvOffsetBasis;
  std::uint64_t pooled_combined_guard = kFnvOffsetBasis;

  for (const auto& [symbol_id, symbol_events] : grouped) {
    CAPTURE(symbol_id);
    const auto stable = replay_events_with_book<OrderBook>(symbol_events, depth);
    const auto pooled = replay_events_with_book<PooledOrderBook>(symbol_events, depth);
    REQUIRE(stable.ok);
    REQUIRE(pooled.ok);
    REQUIRE(stable.events_processed == symbol_events.size());
    require_same_replay_summary(stable, pooled);

    const auto stable_repeat = replay_events_with_book<OrderBook>(symbol_events, depth);
    const auto pooled_repeat = replay_events_with_book<PooledOrderBook>(symbol_events, depth);
    require_same_replay_summary(stable, stable_repeat);
    require_same_replay_summary(pooled, pooled_repeat);

    ReplayConfig replay_config;
    replay_config.validate_sequence_numbers = validate_sequence_numbers;
    ReplayEngine replay(symbol_id, replay_config);
    const ReplayResult replay_result = replay.replay_events(symbol_events);
    INFO(replay_result.error);
    REQUIRE(replay_result.error.empty());
    REQUIRE(replay_result.events_processed == symbol_events.size());
    REQUIRE(replay_result.event_log_checksum == stable.event_log_checksum);
    REQUIRE(replay_result.final_book_checksum == stable.final_book_checksum);
    REQUIRE(replay_result.execution_report_checksum == stable.execution_report_checksum);
    REQUIRE(replay_result.diagnostics_checksum == stable.diagnostics_checksum);

    stable_combined_guard = checksum_append(stable_combined_guard, symbol_id);
    stable_combined_guard = checksum_append(stable_combined_guard, stable.guard_checksum);
    pooled_combined_guard = checksum_append(pooled_combined_guard, symbol_id);
    pooled_combined_guard = checksum_append(pooled_combined_guard, pooled.guard_checksum);
  }

  REQUIRE(pooled_combined_guard == stable_combined_guard);
}

struct CorpusSpec {
  std::string name;
  SyntheticFlowMode mode{SyntheticFlowMode::Balanced};
  std::size_t event_count{0};
  std::uint32_t seed{7};
  PriceTicks price_range_ticks{5};
  std::size_t burst_size{8};
  std::size_t symbol_count{1};
};

std::vector<CorpusSpec> pooled_validation_corpora() {
  return {
      CorpusSpec{"baseline balanced flow", SyntheticFlowMode::Balanced, 240, 20260531U},
      CorpusSpec{"high cancellation rate", SyntheticFlowMode::HighCancellationRate, 260,
                 20260532U},
      CorpusSpec{"replace-heavy flow", SyntheticFlowMode::ReplaceHeavy, 260, 20260533U},
      CorpusSpec{"deep book", SyntheticFlowMode::DeepBook, 320, 20260534U, 12},
      CorpusSpec{"wide price range", SyntheticFlowMode::WidePriceRange, 320, 20260535U, 20},
      CorpusSpec{"bursty flow", SyntheticFlowMode::BurstyFlow, 240, 20260536U, 5, 4},
      CorpusSpec{"long-running same-symbol replay", SyntheticFlowMode::LongRunningSameSymbol,
                 640, 20260537U},
      CorpusSpec{"multi-symbol-style generated input", SyntheticFlowMode::MultiSymbol, 420,
                 20260538U, 5, 8, 4},
      CorpusSpec{"adversarial valid lifecycle sequences", SyntheticFlowMode::AdversarialLifecycle,
                 192, 20260539U},
  };
}

std::vector<MarketDataEvent> make_corpus(const CorpusSpec& spec) {
  SyntheticGeneratorConfig config;
  config.event_count = spec.event_count;
  config.seed = spec.seed;
  config.mode = spec.mode;
  config.price_range_ticks = spec.price_range_ticks;
  config.burst_size = spec.burst_size;
  config.symbol_count = spec.symbol_count;
  return generate_synthetic_events(config);
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

TEST_CASE("Pooled order book matches stable book for adversarial valid lifecycles",
          "[book][pooled][adversarial]") {
  OrderBook stable(1);
  PooledOrderBook pooled(1);
  stable.reserve_order_capacity(16);
  pooled.reserve_order_capacity(16);

  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(10, Side::Buy, 995, 100, 1));
  });
  require_same_result(stable, pooled, [](auto& book) { return book.cancel_order(10); });
  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(10, Side::Buy, 994, 80, 2));
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(10, 994, 120, 3, 3);
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(10, 994, 60, 4, 4);
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(10, 993, 70, 5, 5);
  });
  require_same_result(stable, pooled, [](auto& book) { return book.cancel_order(10); });

  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(20, Side::Sell, 1005, 90, 6));
  });
  require_same_result(stable, pooled, [](auto& book) { return book.reduce_order(20, 30); });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(20, 1006, 50, 7, 7);
  });
  require_same_result(stable, pooled, [](auto& book) { return book.reduce_order(20, 50); });
  REQUIRE(stable.find_order(20) == nullptr);
  REQUIRE(pooled.find_order(20) == nullptr);

  require_same_result(stable, pooled, [](auto& book) {
    return book.add_order(make_order(20, Side::Sell, 1004, 40, 8));
  });
  require_same_result(stable, pooled, [](auto& book) { return book.reduce_order(20, 20); });
  require_same_result(stable, pooled, [](auto& book) { return book.cancel_order(20); });
  REQUIRE(stable.empty());
  REQUIRE(pooled.empty());
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
  require_same_result(stable, pooled, [](auto& book) { return book.reduce_order(99, 1); });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(3, 0, 10, 11, 11);
  });
  require_same_result(stable, pooled, [](auto& book) {
    return book.replace_order(3, 1000, 0, 12, 12);
  });
  require_same_result(stable, pooled, [](auto& book) { return book.reduce_order(3, 0); });
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
    REQUIRE(pooled.execution_report_checksum == stable.execution_report_checksum);
    require_same_l2(stable.final_l2, pooled.final_l2);

    ReplayEngine replay(events.front().symbol_id);
    const ReplayResult replay_result = replay.replay_events(events);
    INFO(replay_result.error);
    REQUIRE(replay_result.error.empty());
    REQUIRE(replay_result.final_book_checksum == pooled.final_book_checksum);
    REQUIRE(replay_result.execution_report_checksum == pooled.execution_report_checksum);
  }
}

TEST_CASE("Pooled order book matches stable book across generated stress corpora",
          "[book][pooled][replay][generated]") {
  for (const CorpusSpec& spec : pooled_validation_corpora()) {
    CAPTURE(spec.name);
    const std::vector<MarketDataEvent> events = make_corpus(spec);
    REQUIRE(events.size() == spec.event_count);
    const bool validate_sequence_numbers = spec.mode != SyntheticFlowMode::MultiSymbol;
    require_book_replay_parity(events, validate_sequence_numbers);
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

TEST_CASE("Pooled generated corpora are allocation-free after warm-up",
          "[book][pooled][alloc][generated]") {
  for (const CorpusSpec& spec : pooled_validation_corpora()) {
    CAPTURE(spec.name);
    const std::vector<MarketDataEvent> events = make_corpus(spec);
    const auto grouped = group_events_by_symbol(events);
    REQUIRE_FALSE(grouped.empty());

    for (const auto& [symbol_id, symbol_events] : grouped) {
      CAPTURE(symbol_id);
      std::uint64_t standard_guard = 0;
      std::uint64_t pooled_guard = 0;

      const AllocationSnapshot standard =
          measure_replay_allocations<OrderBook>(symbol_events, standard_guard);
      const AllocationSnapshot pooled =
          measure_replay_allocations<PooledOrderBook>(symbol_events, pooled_guard);

      REQUIRE(standard_guard == pooled_guard);
      REQUIRE(pooled.allocations == 0);
      REQUIRE(pooled.bytes_allocated == 0);
      if (standard.allocations == 0) {
        INFO("standard path had no measured allocations for this tiny grouped fixture");
      }
    }
  }
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
