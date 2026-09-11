#include "asterion/book/order_book.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

using namespace asterion;

namespace {

std::filesystem::path temp_path(std::string_view suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("asterion_snapshot_" + std::to_string(stamp) + std::string(suffix));
}

// Rest two orders, then a framed snapshot that resets the book and reloads a
// different two-level book. The begin marker carries the first reloaded order.
std::vector<MarketDataEvent> snapshot_stream() {
  return {
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 100, 1, 0, 0},
      MarketDataEvent{2, 2, 1, MarketEventType::Add, Side::Sell, 1001, 50, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Snapshot, Side::Buy, 990, 200, 10, 0,
                      kSnapshotBeginFlag},
      MarketDataEvent{4, 4, 1, MarketEventType::Snapshot, Side::Sell, 1010, 150, 11, 0, 0},
      MarketDataEvent{5, 5, 1, MarketEventType::Snapshot, Side::None, 0, 0, 0, 0,
                      kSnapshotEndFlag},
  };
}

} // namespace

TEST_CASE("Snapshot begin resets the book and reloads resting orders", "[snapshot][replay]") {
  const auto events = snapshot_stream();
  ReplayEngine replay(1);
  const ReplayResult result = replay.replay_events(events);
  INFO(result.error);
  REQUIRE(result.sequence_valid);
  REQUIRE(result.events_processed == events.size());

  // The pre-snapshot orders must be gone; only the snapshot's two levels remain.
  // The reloaded orders carry the snapshot event's timestamp and sequence number.
  OrderBook expected(1);
  REQUIRE(expected.add_order(Order{10, kInvalidClientOrderId, 1, Side::Buy, 990, 200, 3, 3}));
  REQUIRE(expected.add_order(Order{11, kInvalidClientOrderId, 1, Side::Sell, 1010, 150, 4, 4}));

  REQUIRE(result.final_book_checksum == expected.checksum());
  REQUIRE(replay.book().order_count() == 2);
  REQUIRE(replay.book().best_bid() == 990);
  REQUIRE(replay.book().best_ask() == 1010);
}

TEST_CASE("Snapshot streams replay identically from CSV and binary logs",
          "[snapshot][event-log]") {
  const auto events = snapshot_stream();
  const auto csv_path = temp_path(".csv");
  const auto bin_path = temp_path(".bin");
  REQUIRE(write_event_log(csv_path, events, EventLogFormat::Csv).error.empty());
  REQUIRE(write_event_log(bin_path, events, EventLogFormat::Binary).error.empty());

  ReplayEngine csv_replay(1);
  ReplayEngine bin_replay(1);
  const ReplayResult csv_result = csv_replay.replay_file(csv_path, EventLogFormat::Csv);
  const ReplayResult bin_result = bin_replay.replay_file(bin_path, EventLogFormat::Binary);

  INFO(csv_result.error);
  INFO(bin_result.error);
  REQUIRE(csv_result.sequence_valid);
  REQUIRE(bin_result.sequence_valid);
  REQUIRE(csv_result.event_log_checksum == bin_result.event_log_checksum);
  REQUIRE(csv_result.final_book_checksum == bin_result.final_book_checksum);
  REQUIRE(csv_result.execution_report_checksum == bin_result.execution_report_checksum);
  REQUIRE(csv_result.diagnostics_checksum == bin_result.diagnostics_checksum);

  std::filesystem::remove(csv_path);
  std::filesystem::remove(bin_path);
}

TEST_CASE("Snapshot replay is deterministic across runs", "[snapshot][checksum]") {
  const auto events = snapshot_stream();
  ReplayEngine first(1);
  ReplayEngine second(1);
  const ReplayResult a = first.replay_events(events);
  const ReplayResult b = second.replay_events(events);
  REQUIRE(a.final_book_checksum == b.final_book_checksum);
  REQUIRE(a.event_log_checksum == checksum_events(events));
}

TEST_CASE("Snapshot begin marker with no payload clears the book", "[snapshot][replay]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 100, 1, 0, 0},
      MarketDataEvent{2, 2, 1, MarketEventType::Snapshot, Side::None, 0, 0, 0, 0,
                      kSnapshotBeginFlag | kSnapshotEndFlag},
  };
  ReplayEngine replay(1);
  const ReplayResult result = replay.replay_events(events);
  REQUIRE(result.sequence_valid);
  REQUIRE(replay.book().empty());
}

TEST_CASE("Snapshot order with an invalid payload is rejected", "[snapshot][diagnostics]") {
  const std::vector<MarketDataEvent> bad_price{
      MarketDataEvent{1, 1, 1, MarketEventType::Snapshot, Side::Buy, 0, 100, 10, 0,
                      kSnapshotBeginFlag},
  };
  ReplayEngine replay(1);
  const ReplayResult result = replay.replay_events(bad_price);
  REQUIRE_FALSE(result.sequence_valid);
  REQUIRE(result.diagnostics.front().reason.find("invalid snapshot price") != std::string::npos);
}
