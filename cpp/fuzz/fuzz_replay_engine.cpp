#include "fuzz_support.hpp"

#include "asterion/market_data/replay.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

using namespace asterion;

namespace {

[[nodiscard]] MarketEventType event_type_from_byte(std::uint8_t byte) {
  switch (byte % 7U) {
  case 0:
    return MarketEventType::Add;
  case 1:
    return MarketEventType::Cancel;
  case 2:
    return MarketEventType::Replace;
  case 3:
    return MarketEventType::Execute;
  case 4:
    return MarketEventType::Trade;
  case 5:
    return MarketEventType::Snapshot;
  default:
    return MarketEventType::Heartbeat;
  }
}

[[nodiscard]] std::vector<MarketDataEvent> map_events(std::span<const std::uint8_t> input) {
  if (input.empty()) {
    return {};
  }

  fuzz::ByteReader reader(input);
  const std::size_t count =
      std::min(fuzz::kMaxGeneratedEvents, std::max<std::size_t>(1U, input.size() / 8U));
  std::vector<MarketDataEvent> events;
  events.reserve(count);
  TimestampNs timestamp = 1;
  SequenceNumber sequence = 1;

  for (std::size_t index = 0; index < count; ++index) {
    const std::uint8_t type = reader.next();
    const std::uint8_t side = reader.next();
    const std::uint8_t price = reader.next();
    const std::uint8_t quantity = reader.next();
    const std::uint8_t order = reader.next();
    const std::uint8_t symbol = reader.next();
    const std::uint8_t sequence_delta = reader.next();
    const std::uint8_t timestamp_delta = reader.next();

    sequence += static_cast<SequenceNumber>(sequence_delta % 4U);
    timestamp += static_cast<TimestampNs>(static_cast<int>(timestamp_delta % 9U) - 3);
    events.push_back(MarketDataEvent{
        timestamp,
        sequence,
        static_cast<SymbolId>(symbol % 3U),
        event_type_from_byte(type),
        fuzz::side_from_byte(side),
        static_cast<PriceTicks>(static_cast<int>(price % 21U) - 5),
        static_cast<Quantity>(static_cast<int>(quantity % 21U) - 5),
        static_cast<OrderId>(order % 24U),
        static_cast<TradeId>(index + 1U),
        static_cast<std::uint32_t>(type & 0x3U),
    });
  }
  return events;
}

[[nodiscard]] bool same_replay_result(const ReplayResult& first, const ReplayResult& second) {
  if (first.events_processed != second.events_processed ||
      first.sequence_valid != second.sequence_valid ||
      first.event_log_checksum != second.event_log_checksum ||
      first.final_book_checksum != second.final_book_checksum ||
      first.execution_report_checksum != second.execution_report_checksum ||
      first.diagnostics_checksum != second.diagnostics_checksum ||
      first.diagnostic_error_count != second.diagnostic_error_count ||
      first.diagnostic_warning_count != second.diagnostic_warning_count ||
      first.error != second.error || first.diagnostics.size() != second.diagnostics.size()) {
    return false;
  }
  for (std::size_t index = 0; index < first.diagnostics.size(); ++index) {
    if (first.diagnostics[index].reason != second.diagnostics[index].reason) {
      return false;
    }
  }
  return true;
}

void require_book_ok(const OrderBook& book) {
  fuzz::require(book.check_invariants().ok);
  const auto best_bid = book.best_bid();
  const auto best_ask = book.best_ask();
  fuzz::require(!best_bid || !best_ask || *best_bid < *best_ask);
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > fuzz::kMaxParserInputSize) {
    return 0;
  }

  const std::span<const std::uint8_t> input(data, size);
  std::vector<MarketDataEvent> events;
  const auto path = fuzz::write_temp_input("replay-engine", ".log", input);
  if (path) {
    EventLogReadResult parsed = read_event_log(*path, EventLogFormat::Auto);
    fuzz::remove_temp_input(path);
    if (parsed.error.empty()) {
      events = std::move(parsed.events);
      if (events.size() > fuzz::kMaxGeneratedEvents) {
        events.resize(fuzz::kMaxGeneratedEvents);
      }
    }
  }
  if (events.empty()) {
    events = map_events(input);
  }

  ReplayEngine first_engine(1);
  ReplayEngine second_engine(1);
  const ReplayResult first = first_engine.replay_events(events);
  const ReplayResult second = second_engine.replay_events(events);

  fuzz::require(same_replay_result(first, second));
  fuzz::require(first.events_processed <= events.size());
  fuzz::require(first.diagnostic_error_count + first.diagnostic_warning_count <=
                first.diagnostics.size());
  require_book_ok(first_engine.book());
  require_book_ok(second_engine.book());
  return 0;
}
