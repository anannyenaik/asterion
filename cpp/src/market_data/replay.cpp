#include "asterion/market_data/replay.hpp"

#include "asterion/core/checksum.hpp"

#include <fstream>
#include <string>
#include <vector>

namespace asterion {

namespace {

[[nodiscard]] bool is_ignored_line(const std::string& line) {
  const auto first = line.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) {
    return true;
  }
  if (line[first] == '#') {
    return true;
  }
  return line.find("timestamp_ns") != std::string::npos;
}

} // namespace

ReplayEngine::ReplayEngine(SymbolId symbol_id, ReplayConfig config)
    : config_(config), book_(symbol_id) {}

ReplayResult ReplayEngine::replay_file(const std::filesystem::path& path) {
  std::ifstream input(path);
  ReplayResult result;
  result.execution_report_checksum = kFnvOffsetBasis;
  if (!input) {
    result.error = "unable to open replay file: " + path.string();
    result.sequence_valid = false;
    return result;
  }

  std::vector<MarketDataEvent> events;
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    if (is_ignored_line(line)) {
      continue;
    }
    std::string error;
    auto event = parse_market_data_event_csv(line, &error);
    if (!event) {
      result.error = "line " + std::to_string(line_number) + ": " + error;
      result.sequence_valid = false;
      return result;
    }
    events.push_back(*event);
  }

  return replay_events(events);
}

ReplayResult ReplayEngine::replay_events(std::span<const MarketDataEvent> events) {
  ReplayResult result;
  result.execution_report_checksum = kFnvOffsetBasis;
  SequenceNumber expected_sequence = 0;
  TimestampNs last_timestamp = 0;
  bool has_last_timestamp = false;

  for (const MarketDataEvent& event : events) {
    if (config_.validate_sequence_numbers) {
      if (expected_sequence == 0) {
        expected_sequence = event.sequence_number;
      }
      if (event.sequence_number != expected_sequence) {
        result.sequence_valid = false;
        result.error = "non-contiguous sequence number at event " +
                       std::to_string(result.events_processed + 1U);
        return result;
      }
      ++expected_sequence;
    }
    if (config_.validate_timestamps) {
      if (has_last_timestamp && event.timestamp_ns < last_timestamp) {
        result.sequence_valid = false;
        result.error =
            "timestamp reversal at event " + std::to_string(result.events_processed + 1U);
        return result;
      }
      last_timestamp = event.timestamp_ns;
      has_last_timestamp = true;
    }

    if (!apply_event(event, result)) {
      result.sequence_valid = false;
      return result;
    }
    ++result.events_processed;
  }

  result.final_book_checksum = book_.checksum();
  return result;
}

bool ReplayEngine::apply_event(const MarketDataEvent& event, ReplayResult& result) {
  if (event.symbol_id != book_.symbol_id()) {
    result.error = "event symbol does not match replay book symbol";
    return false;
  }

  switch (event.event_type) {
  case MarketEventType::Add: {
    const bool ok = book_.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                          event.side, event.price_ticks, event.quantity,
                                          event.timestamp_ns, event.sequence_number});
    if (!ok) {
      result.error = "failed to apply Add event";
      return false;
    }
    break;
  }
  case MarketEventType::Cancel:
    if (event.quantity > 0) {
      if (!book_.reduce_order(event.order_id, event.quantity)) {
        result.error = "failed to apply partial Cancel event";
        return false;
      }
    } else if (!book_.cancel_order(event.order_id)) {
      result.error = "failed to apply Cancel event";
      return false;
    }
    break;
  case MarketEventType::Replace:
    if (!book_.replace_order(event.order_id, event.price_ticks, event.quantity, event.timestamp_ns,
                             event.sequence_number)) {
      result.error = "failed to apply Replace event";
      return false;
    }
    break;
  case MarketEventType::Execute:
    if (!book_.reduce_order(event.order_id, event.quantity)) {
      result.error = "failed to apply Execute event";
      return false;
    }
    update_activity_checksum(event, result);
    break;
  case MarketEventType::Trade:
    update_activity_checksum(event, result);
    break;
  case MarketEventType::Snapshot:
  case MarketEventType::Heartbeat:
    break;
  }

  const auto invariant_report = book_.check_invariants();
  if (!invariant_report.ok) {
    result.error = "book invariant violation after event " + std::to_string(event.sequence_number);
    return false;
  }
  return true;
}

void ReplayEngine::update_activity_checksum(const MarketDataEvent& event, ReplayResult& result) const {
  result.execution_report_checksum = append_to_checksum(result.execution_report_checksum, event);
}

} // namespace asterion
