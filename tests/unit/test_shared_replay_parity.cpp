// Grouped-vs-shared replay parity coverage for the opt-in shared multi-symbol
// replay path. Grouped replay (replay_by_symbol) is the correctness-first default;
// these tests demonstrate, for the tested deterministic cases, that the opt-in
// shared path (replay_shared_by_symbol) agrees with it across interleaved symbols,
// snapshots, cancels, replaces, sequence gaps, timestamp reversals, invalid events
// and fixed-seed random corpora. They do not prove parity for all workloads.
//
// The parity contract is documented in docs/shared_replay_parity.md.

#include "asterion/market_data/event.hpp"
#include "asterion/market_data/replay_aggregate.hpp"
#include "asterion/market_data/synthetic_generator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

using namespace asterion;

namespace {

// Assert full grouped-vs-shared parity for one case and that the result is
// deterministic across a repeated run. On failure, the structured
// describe_replay_parity diagnostic is attached so the mismatch is reproducible
// from the test log alone (case name, differing fields, first differing
// diagnostic, grouped expected L2).
void require_parity(std::string_view name, const std::vector<MarketDataEvent>& events,
                    AggregateReplayConfig config = {}) {
  const ReplayParityReport report = compare_replay_parity(events, config);
  INFO("case: " << name);
  if (!report.matched) {
    INFO(describe_replay_parity(events, config));
  }
  REQUIRE(report.matched);
  REQUIRE(report.mismatch_count == 0);
  REQUIRE(report.combined_book_checksum_match);
  REQUIRE(report.aggregate_checksum_match);
  REQUIRE(report.symbol_count_grouped == report.symbol_count_shared);
  for (const SymbolParityEntry& entry : report.symbols) {
    INFO("symbol " << entry.symbol_id);
    REQUIRE(entry.present_in_grouped);
    REQUIRE(entry.present_in_shared);
    REQUIRE(entry.matched);
  }

  // Determinism: a second run must reproduce identical checksums on both paths.
  const ReplayParityReport again = compare_replay_parity(events, config);
  REQUIRE(again.matched);
  REQUIRE(again.grouped_aggregate_checksum == report.grouped_aggregate_checksum);
  REQUIRE(again.shared_aggregate_checksum == report.shared_aggregate_checksum);
  REQUIRE(again.grouped_combined_book_checksum == report.grouped_combined_book_checksum);
}

AggregateReplayConfig strict_sequences() {
  AggregateReplayConfig config;
  config.validate_per_symbol_sequences = true;
  return config;
}

} // namespace

TEST_CASE("Parity: two-symbol interleaved add/cancel/replace flow", "[parity][golden]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Add, Side::Buy, 985, 5, 3, 0, 0},
      MarketDataEvent{4, 4, 2, MarketEventType::Replace, Side::Sell, 1012, 6, 2, 0, 0},
      MarketDataEvent{5, 5, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 3, 0, 0},
      MarketDataEvent{6, 6, 2, MarketEventType::Add, Side::Buy, 1000, 4, 4, 0, 0},
      MarketDataEvent{7, 7, 1, MarketEventType::Replace, Side::Buy, 991, 12, 1, 0, 0},
  };
  require_parity("two-symbol interleaved", events);
}

TEST_CASE("Parity: three-symbol interleaving", "[parity][golden]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
      MarketDataEvent{3, 3, 3, MarketEventType::Add, Side::Buy, 500, 3, 3, 0, 0},
      MarketDataEvent{4, 4, 1, MarketEventType::Add, Side::Sell, 1005, 7, 4, 0, 0},
      MarketDataEvent{5, 5, 2, MarketEventType::Cancel, Side::Sell, 0, 0, 2, 0, 0},
      MarketDataEvent{6, 6, 3, MarketEventType::Replace, Side::Buy, 501, 9, 3, 0, 0},
      MarketDataEvent{7, 7, 1, MarketEventType::Execute, Side::Sell, 1005, 3, 4, 77, 0},
      MarketDataEvent{8, 8, 3, MarketEventType::Add, Side::Sell, 510, 2, 5, 0, 0},
  };
  require_parity("three-symbol interleaving", events);
}

TEST_CASE("Parity: multi-symbol snapshot begin/end flow", "[parity][snapshot]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Buy, 480, 20, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Snapshot, Side::None, 0, 0, 0, 0,
                      kSnapshotBeginFlag},
      MarketDataEvent{4, 4, 1, MarketEventType::Snapshot, Side::Buy, 989, 7, 10, 0, 0},
      MarketDataEvent{5, 5, 1, MarketEventType::Snapshot, Side::Sell, 1011, 6, 11, 0, 0},
      MarketDataEvent{6, 6, 1, MarketEventType::Snapshot, Side::None, 0, 0, 0, 0,
                      kSnapshotEndFlag},
      MarketDataEvent{7, 7, 2, MarketEventType::Replace, Side::Buy, 481, 15, 2, 0, 0},
      MarketDataEvent{8, 8, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 10, 0, 0},
  };
  require_parity("multi-symbol snapshot", events);
}

TEST_CASE("Parity: cancel after symbol interleaving", "[parity][golden]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Buy, 480, 12, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Add, Side::Buy, 989, 6, 3, 0, 0},
      MarketDataEvent{4, 4, 2, MarketEventType::Add, Side::Buy, 479, 4, 4, 0, 0},
      MarketDataEvent{5, 5, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 1, 0, 0},
      MarketDataEvent{6, 6, 2, MarketEventType::Cancel, Side::Buy, 0, 0, 2, 0, 0},
      MarketDataEvent{7, 7, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 3, 0, 0},
  };
  require_parity("cancel after interleaving", events);
}

TEST_CASE("Parity: replace-heavy interleaving", "[parity][replace]") {
  std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
  };
  // Alternate repeated replaces on the two resting orders across both symbols.
  TimestampNs ts = 3;
  SequenceNumber seq = 3;
  for (int i = 0; i < 12; ++i) {
    const bool symbol_one = (i % 2) == 0;
    const SymbolId symbol = symbol_one ? SymbolId{1} : SymbolId{2};
    const OrderId order = symbol_one ? OrderId{1} : OrderId{2};
    const Side side = symbol_one ? Side::Buy : Side::Sell;
    const PriceTicks price =
        symbol_one ? static_cast<PriceTicks>(985 + i) : static_cast<PriceTicks>(1015 - i);
    events.push_back(MarketDataEvent{ts++, seq++, symbol, MarketEventType::Replace, side, price,
                                     static_cast<Quantity>(5 + i), order, 0, 0});
  }
  require_parity("replace-heavy", events);
}

TEST_CASE("Parity: sequence gap on one symbol while another continues",
          "[parity][failure][sequence]") {
  // Symbol 1 has a per-symbol sequence gap (1 -> 3); symbol 2 stays contiguous (1, 2).
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 1, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 1, 0, 0}, // gap
      MarketDataEvent{4, 2, 2, MarketEventType::Replace, Side::Sell, 1011, 6, 2, 0, 0},
  };
  require_parity("sequence gap one symbol", events, strict_sequences());
}

TEST_CASE("Parity: timestamp reversal on one symbol", "[parity][failure][timestamp]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{10, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{11, 1, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
      MarketDataEvent{5, 2, 1, MarketEventType::Replace, Side::Buy, 991, 12, 1, 0, 0}, // reversal
      MarketDataEvent{12, 2, 2, MarketEventType::Cancel, Side::Sell, 0, 0, 2, 0, 0},
  };
  require_parity("timestamp reversal one symbol", events);
}

TEST_CASE("Parity: invalid event on one symbol", "[parity][failure]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Add, Side::Buy, -1, 5, 3, 0, 0}, // invalid price
      MarketDataEvent{4, 4, 2, MarketEventType::Cancel, Side::Sell, 0, 0, 2, 0, 0},
  };
  require_parity("invalid event one symbol", events);
}

TEST_CASE("Parity: duplicate order id across different symbols is allowed by the namespace",
          "[parity][namespace]") {
  // Order ids are per-symbol: the same id resting on two different symbols is not a
  // duplicate. Both paths route by symbol, so both adds succeed and parity holds.
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 42, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1010, 8, 42, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 42, 0, 0},
      MarketDataEvent{4, 4, 2, MarketEventType::Replace, Side::Sell, 1011, 6, 42, 0, 0},
  };
  require_parity("duplicate order id across symbols", events);
}

TEST_CASE("Parity: duplicate order id within the same symbol is rejected on both paths",
          "[parity][failure][namespace]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 7, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Add, Side::Buy, 991, 5, 7, 0, 0}, // duplicate
  };
  require_parity("duplicate order id same symbol", events);

  // The shared and grouped paths must both surface the duplicate as an error.
  const AggregateReplaySummary grouped = replay_by_symbol(events);
  std::size_t errors = 0;
  for (const SymbolReplaySummary& summary : grouped.symbols) {
    errors += summary.diagnostic_error_count;
  }
  REQUIRE(errors >= 1);
}

TEST_CASE("Parity: combined failure modes interleaved across symbols", "[parity][failure]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 1, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 1, 0, 0}, // seq gap
      MarketDataEvent{4, 2, 2, MarketEventType::Cancel, Side::Sell, 0, 0, 99, 0, 0}, // unknown
      MarketDataEvent{5, 1, 3, MarketEventType::Add, Side::Buy, 500, 4, 5, 0, 0},
      MarketDataEvent{6, 2, 3, MarketEventType::Add, Side::Buy, 500, 4, 5, 0, 0}, // dup same symbol
  };
  require_parity("combined failure modes", events, strict_sequences());
}

TEST_CASE("Parity: describe_replay_parity reports a clean match", "[parity][diagnostics]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 990, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1010, 8, 2, 0, 0},
  };
  REQUIRE(describe_replay_parity(events) == "replay parity matched");
}

TEST_CASE("Parity holds across fixed seeds, symbol counts and flow modes",
          "[parity][property]") {
  constexpr std::array<std::uint32_t, 12> seeds{1U,      2U,      3U,      7U,
                                                17U,     4099U,   123457U, 20260606U,
                                                900001U, 555U,    88888U,  31337U};
  constexpr std::array<std::size_t, 4> symbol_counts{2U, 3U, 5U, 8U};
  constexpr std::array<SyntheticFlowMode, 3> modes{SyntheticFlowMode::MultiSymbol,
                                                    SyntheticFlowMode::ReplaceHeavy,
                                                    SyntheticFlowMode::HighCancellationRate};
  for (const std::uint32_t seed : seeds) {
    for (const std::size_t symbols : symbol_counts) {
      for (const SyntheticFlowMode mode : modes) {
        SyntheticGeneratorConfig config;
        config.event_count = 250;
        config.seed = seed;
        config.mode = mode;
        config.symbol_count = symbols;
        const std::vector<MarketDataEvent> events = generate_synthetic_events(config);
        require_parity("seed=" + std::to_string(seed) + " symbols=" + std::to_string(symbols) +
                           " mode=" + std::to_string(static_cast<int>(mode)),
                       events);
      }
    }
  }
}
