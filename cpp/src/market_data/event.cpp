#include "asterion/market_data/event.hpp"

#include "asterion/core/checksum.hpp"

#include <array>
#include <charconv>
#include <sstream>
#include <vector>

namespace asterion {

namespace {

[[nodiscard]] std::string trim_copy(std::string_view value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(begin, end - begin + 1));
}

[[nodiscard]] std::vector<std::string> split_csv(std::string_view line) {
  std::vector<std::string> fields;
  std::size_t start = 0;
  while (start <= line.size()) {
    const auto comma = line.find(',', start);
    if (comma == std::string_view::npos) {
      fields.push_back(trim_copy(line.substr(start)));
      break;
    }
    fields.push_back(trim_copy(line.substr(start, comma - start)));
    start = comma + 1;
  }
  return fields;
}

template <typename T> [[nodiscard]] std::optional<T> parse_integral(const std::string& value) {
  T output{};
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(first, last, output);
  if (ec != std::errc() || ptr != last) {
    return std::nullopt;
  }
  return output;
}

} // namespace

std::string_view to_string(MarketEventType type) noexcept {
  switch (type) {
  case MarketEventType::Add:
    return "Add";
  case MarketEventType::Cancel:
    return "Cancel";
  case MarketEventType::Replace:
    return "Replace";
  case MarketEventType::Execute:
    return "Execute";
  case MarketEventType::Trade:
    return "Trade";
  case MarketEventType::Snapshot:
    return "Snapshot";
  case MarketEventType::Heartbeat:
    return "Heartbeat";
  }
  return "Unknown";
}

std::optional<MarketEventType> parse_market_event_type(std::string_view value) {
  const std::string token = trim_copy(value);
  if (token == "Add" || token == "A") {
    return MarketEventType::Add;
  }
  if (token == "Cancel" || token == "C") {
    return MarketEventType::Cancel;
  }
  if (token == "Replace" || token == "R") {
    return MarketEventType::Replace;
  }
  if (token == "Execute" || token == "E") {
    return MarketEventType::Execute;
  }
  if (token == "Trade" || token == "T") {
    return MarketEventType::Trade;
  }
  if (token == "Snapshot" || token == "S") {
    return MarketEventType::Snapshot;
  }
  if (token == "Heartbeat" || token == "H") {
    return MarketEventType::Heartbeat;
  }
  return std::nullopt;
}

std::optional<Side> parse_side(std::string_view value) {
  const std::string token = trim_copy(value);
  if (token == "Buy" || token == "B") {
    return Side::Buy;
  }
  if (token == "Sell" || token == "S") {
    return Side::Sell;
  }
  if (token == "None" || token == "N" || token.empty()) {
    return Side::None;
  }
  return std::nullopt;
}

std::optional<MarketDataEvent> parse_market_data_event_csv(std::string_view line,
                                                           std::string* error) {
  const auto fields = split_csv(line);
  if (fields.size() != 10U) {
    if (error != nullptr) {
      *error = "expected 10 CSV fields";
    }
    return std::nullopt;
  }

  auto timestamp = parse_integral<TimestampNs>(fields[0]);
  auto sequence = parse_integral<SequenceNumber>(fields[1]);
  auto symbol = parse_integral<SymbolId>(fields[2]);
  auto event_type = parse_market_event_type(fields[3]);
  auto side = parse_side(fields[4]);
  auto price = parse_integral<PriceTicks>(fields[5]);
  auto quantity = parse_integral<Quantity>(fields[6]);
  auto order_id = parse_integral<OrderId>(fields[7]);
  auto trade_id = parse_integral<TradeId>(fields[8]);
  auto flags = parse_integral<std::uint32_t>(fields[9]);

  if (!timestamp || !sequence || !symbol || !event_type || !side || !price || !quantity ||
      !order_id || !trade_id || !flags) {
    if (error != nullptr) {
      *error = "failed to parse one or more CSV fields";
    }
    return std::nullopt;
  }

  return MarketDataEvent{*timestamp, *sequence, *symbol, *event_type, *side, *price,
                         *quantity,  *order_id, *trade_id, *flags};
}

std::string market_data_event_to_csv(const MarketDataEvent& event) {
  std::ostringstream stream;
  stream << event.timestamp_ns << ',' << event.sequence_number << ',' << event.symbol_id << ','
         << to_string(event.event_type) << ',' << to_string(event.side) << ','
         << event.price_ticks << ',' << event.quantity << ',' << event.order_id << ','
         << event.trade_id << ',' << event.flags;
  return stream.str();
}

std::uint64_t append_to_checksum(std::uint64_t seed, const MarketDataEvent& event) noexcept {
  seed = checksum_append(seed, event.timestamp_ns);
  seed = checksum_append(seed, event.sequence_number);
  seed = checksum_append(seed, event.symbol_id);
  seed = checksum_append(seed, event.event_type);
  seed = checksum_append(seed, event.side);
  seed = checksum_append(seed, event.price_ticks);
  seed = checksum_append(seed, event.quantity);
  seed = checksum_append(seed, event.order_id);
  seed = checksum_append(seed, event.trade_id);
  seed = checksum_append(seed, event.flags);
  return seed;
}

} // namespace asterion
