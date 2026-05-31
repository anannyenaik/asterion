#include "asterion/market_data/event_log.hpp"

#include "asterion/core/checksum.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace asterion {

namespace {

using HeaderBytes = std::array<std::uint8_t, kBinaryEventLogHeaderSize>;
using RecordBytes = std::array<std::uint8_t, kBinaryEventRecordSize>;

inline constexpr std::string_view kEventLogSchemaMigrationHint =
    "See docs/event_log_schema.md for the event-log schema migration/versioning checklist.";

[[nodiscard]] std::string schema_error(std::string_view category, std::string_view detail) {
  return std::string(category) + ": " + std::string(detail) + ". " +
         std::string(kEventLogSchemaMigrationHint);
}

[[nodiscard]] std::string trim_ascii_copy(std::string_view value) {
  const auto begin = value.find_first_not_of(" \t\r\n");
  if (begin == std::string_view::npos) {
    return {};
  }
  const auto end = value.find_last_not_of(" \t\r\n");
  return std::string(value.substr(begin, end - begin + 1U));
}

[[nodiscard]] bool is_blank_or_comment_csv_line(std::string_view trimmed) {
  return trimmed.empty() || trimmed.front() == '#';
}

[[nodiscard]] bool looks_like_csv_header(std::string_view line) {
  constexpr std::array<std::string_view, 10> columns{
      "timestamp_ns", "sequence_number", "symbol_id", "event_type", "side",
      "price_ticks",  "quantity",        "order_id",  "trade_id",   "flags"};
  std::size_t matches = 0;
  for (const std::string_view column : columns) {
    if (line.find(column) != std::string_view::npos) {
      ++matches;
    }
  }
  return matches >= 2U;
}

[[nodiscard]] bool is_ignored_csv_line(const std::string& line, std::string* error) {
  const std::string trimmed = trim_ascii_copy(line);
  if (is_blank_or_comment_csv_line(trimmed)) {
    return true;
  }
  if (trimmed == kMarketDataCsvHeader) {
    return true;
  }
  if (looks_like_csv_header(trimmed)) {
    if (error != nullptr) {
      *error = schema_error("CSV column drift",
                            "expected v1 header \"" + std::string(kMarketDataCsvHeader) +
                                "\", got \"" + trimmed + "\"");
    }
    return true;
  }
  return false;
}

[[nodiscard]] std::string lower_copy(std::string_view value) {
  std::string output(value);
  std::transform(output.begin(), output.end(), output.begin(), [](unsigned char c) {
    return static_cast<char>(std::tolower(c));
  });
  return output;
}

template <typename T> void append_little_endian(std::vector<std::uint8_t>& output, T input) {
  static_assert(std::is_integral_v<T>);
  using Unsigned = std::make_unsigned_t<T>;
  auto value = static_cast<Unsigned>(input);
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    output.push_back(static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU));
  }
}

template <typename T>
[[nodiscard]] T read_little_endian(const RecordBytes& bytes, std::size_t offset) {
  static_assert(std::is_integral_v<T>);
  using Unsigned = std::make_unsigned_t<T>;
  Unsigned value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<Unsigned>(bytes[offset + i]) << (i * 8U);
  }
  return static_cast<T>(value);
}

[[nodiscard]] std::uint16_t read_header_u16(const HeaderBytes& bytes, std::size_t offset) {
  const auto low = static_cast<std::uint16_t>(bytes[offset]);
  const auto high = static_cast<std::uint16_t>(
      static_cast<std::uint16_t>(bytes[offset + 1U]) << 8U);
  return static_cast<std::uint16_t>(low | high);
}

[[nodiscard]] std::optional<MarketEventType> event_type_from_wire(std::uint8_t value) {
  switch (value) {
  case static_cast<std::uint8_t>(MarketEventType::Add):
    return MarketEventType::Add;
  case static_cast<std::uint8_t>(MarketEventType::Cancel):
    return MarketEventType::Cancel;
  case static_cast<std::uint8_t>(MarketEventType::Replace):
    return MarketEventType::Replace;
  case static_cast<std::uint8_t>(MarketEventType::Execute):
    return MarketEventType::Execute;
  case static_cast<std::uint8_t>(MarketEventType::Trade):
    return MarketEventType::Trade;
  case static_cast<std::uint8_t>(MarketEventType::Snapshot):
    return MarketEventType::Snapshot;
  case static_cast<std::uint8_t>(MarketEventType::Heartbeat):
    return MarketEventType::Heartbeat;
  default:
    return std::nullopt;
  }
}

[[nodiscard]] std::optional<Side> side_from_wire(std::uint8_t value) {
  switch (value) {
  case static_cast<std::uint8_t>(Side::None):
    return Side::None;
  case static_cast<std::uint8_t>(Side::Buy):
    return Side::Buy;
  case static_cast<std::uint8_t>(Side::Sell):
    return Side::Sell;
  default:
    return std::nullopt;
  }
}

[[nodiscard]] std::vector<std::uint8_t> encode_header() {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(kBinaryEventLogHeaderSize);
  bytes.insert(bytes.end(), kBinaryEventLogMagic.begin(), kBinaryEventLogMagic.end());
  append_little_endian<std::uint16_t>(bytes, kBinaryEventLogVersion);
  append_little_endian<std::uint16_t>(bytes, kBinaryEventLogHeaderSize);
  append_little_endian<std::uint16_t>(bytes, kBinaryEventRecordSize);
  append_little_endian<std::uint16_t>(bytes, 0);
  return bytes;
}

[[nodiscard]] std::vector<std::uint8_t> encode_record(const MarketDataEvent& event) {
  std::vector<std::uint8_t> bytes;
  bytes.reserve(kBinaryEventRecordSize);
  append_little_endian<TimestampNs>(bytes, event.timestamp_ns);
  append_little_endian<SequenceNumber>(bytes, event.sequence_number);
  append_little_endian<SymbolId>(bytes, event.symbol_id);
  append_little_endian<std::uint8_t>(bytes, static_cast<std::uint8_t>(event.event_type));
  append_little_endian<std::uint8_t>(bytes, static_cast<std::uint8_t>(event.side));
  append_little_endian<std::uint32_t>(bytes, event.flags);
  append_little_endian<PriceTicks>(bytes, event.price_ticks);
  append_little_endian<Quantity>(bytes, event.quantity);
  append_little_endian<OrderId>(bytes, event.order_id);
  append_little_endian<TradeId>(bytes, event.trade_id);
  return bytes;
}

[[nodiscard]] std::optional<MarketDataEvent> decode_record(const RecordBytes& bytes,
                                                           std::size_t record_index,
                                                           std::string* error) {
  const auto event_type = event_type_from_wire(bytes[20]);
  if (!event_type) {
    if (error != nullptr) {
      *error = schema_error("enum drift",
                            "record " + std::to_string(record_index) +
                                ": invalid MarketEventType wire value " +
                                std::to_string(bytes[20]));
    }
    return std::nullopt;
  }

  const auto side = side_from_wire(bytes[21]);
  if (!side) {
    if (error != nullptr) {
      *error = schema_error("enum drift",
                            "record " + std::to_string(record_index) +
                                ": invalid Side wire value " + std::to_string(bytes[21]));
    }
    return std::nullopt;
  }

  return MarketDataEvent{
      read_little_endian<TimestampNs>(bytes, 0),
      read_little_endian<SequenceNumber>(bytes, 8),
      read_little_endian<SymbolId>(bytes, 16),
      *event_type,
      *side,
      read_little_endian<PriceTicks>(bytes, 26),
      read_little_endian<Quantity>(bytes, 34),
      read_little_endian<OrderId>(bytes, 42),
      read_little_endian<TradeId>(bytes, 50),
      read_little_endian<std::uint32_t>(bytes, 22)};
}

[[nodiscard]] EventLogReadResult read_csv_event_log(const std::filesystem::path& path) {
  std::ifstream input(path);
  EventLogReadResult result;
  result.detected_format = EventLogFormat::Csv;
  result.event_checksum = kFnvOffsetBasis;
  if (!input) {
    result.error = "unable to open CSV event log: " + path.string();
    return result;
  }

  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    std::string header_error;
    if (is_ignored_csv_line(line, &header_error)) {
      if (!header_error.empty()) {
        result.error = "line " + std::to_string(line_number) + ": " + header_error;
        result.events.clear();
        result.event_checksum = kFnvOffsetBasis;
        return result;
      }
      continue;
    }
    std::string error;
    auto event = parse_market_data_event_csv(line, &error);
    if (!event) {
      result.error = "line " + std::to_string(line_number) + ": " + error;
      result.events.clear();
      result.event_checksum = kFnvOffsetBasis;
      return result;
    }
    result.event_checksum = append_to_checksum(result.event_checksum, *event);
    result.events.push_back(*event);
  }

  return result;
}

[[nodiscard]] EventLogReadResult read_binary_event_log(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  EventLogReadResult result;
  result.detected_format = EventLogFormat::Binary;
  result.event_checksum = kFnvOffsetBasis;
  if (!input) {
    result.error = "unable to open binary event log: " + path.string();
    return result;
  }

  HeaderBytes header{};
  input.read(reinterpret_cast<char*>(header.data()), static_cast<std::streamsize>(header.size()));
  if (input.gcount() != static_cast<std::streamsize>(header.size())) {
    result.error = schema_error("malformed binary event log", "truncated binary event-log header");
    return result;
  }

  const std::string_view magic(reinterpret_cast<const char*>(header.data()),
                               kBinaryEventLogMagic.size());
  if (magic != kBinaryEventLogMagic) {
    result.error = schema_error("binary header drift",
                                "invalid binary event-log magic, expected \"" +
                                    std::string(kBinaryEventLogMagic) + "\"");
    return result;
  }

  const std::uint16_t version = read_header_u16(header, 8);
  const std::uint16_t header_size = read_header_u16(header, 10);
  const std::uint16_t record_size = read_header_u16(header, 12);
  const std::uint16_t reserved = read_header_u16(header, 14);
  if (version != kBinaryEventLogVersion) {
    result.error = schema_error("binary schema version drift",
                                "unsupported binary event-log version " +
                                    std::to_string(version) + ", expected " +
                                    std::to_string(kBinaryEventLogVersion));
    return result;
  }
  if (header_size != kBinaryEventLogHeaderSize) {
    result.error = schema_error("binary layout drift",
                                "unsupported binary event-log header size " +
                                    std::to_string(header_size) + ", expected " +
                                    std::to_string(kBinaryEventLogHeaderSize));
    return result;
  }
  if (record_size != kBinaryEventRecordSize) {
    result.error = schema_error("binary layout drift",
                                "unsupported binary event record size " +
                                    std::to_string(record_size) + ", expected " +
                                    std::to_string(kBinaryEventRecordSize));
    return result;
  }
  if (reserved != 0U) {
    result.error =
        schema_error("binary header drift",
                     "reserved header field must be zero in schema v1, got " +
                         std::to_string(reserved));
    return result;
  }

  std::size_t record_index = 0;
  while (true) {
    RecordBytes record{};
    input.read(reinterpret_cast<char*>(record.data()), static_cast<std::streamsize>(record.size()));
    const std::streamsize bytes_read = input.gcount();
    if (bytes_read == 0) {
      break;
    }
    if (bytes_read != static_cast<std::streamsize>(record.size())) {
      result.error = schema_error("malformed binary event log",
                                  "truncated binary event record at index " +
                                      std::to_string(record_index));
      result.events.clear();
      result.event_checksum = kFnvOffsetBasis;
      return result;
    }

    std::string error;
    auto event = decode_record(record, record_index, &error);
    if (!event) {
      result.error = error;
      result.events.clear();
      result.event_checksum = kFnvOffsetBasis;
      return result;
    }

    result.event_checksum = append_to_checksum(result.event_checksum, *event);
    result.events.push_back(*event);
    ++record_index;
  }

  return result;
}

[[nodiscard]] EventLogWriteResult write_csv_event_log(const std::filesystem::path& path,
                                                      std::span<const MarketDataEvent> events) {
  std::ofstream output(path);
  EventLogWriteResult result;
  result.event_checksum = kFnvOffsetBasis;
  if (!output) {
    result.error = "unable to write CSV event log: " + path.string();
    return result;
  }

  output << kMarketDataCsvHeader << '\n';
  for (const MarketDataEvent& event : events) {
    output << market_data_event_to_csv(event) << '\n';
    result.event_checksum = append_to_checksum(result.event_checksum, event);
    ++result.events_written;
  }
  return result;
}

[[nodiscard]] EventLogWriteResult write_binary_event_log(const std::filesystem::path& path,
                                                         std::span<const MarketDataEvent> events) {
  std::ofstream output(path, std::ios::binary);
  EventLogWriteResult result;
  result.event_checksum = kFnvOffsetBasis;
  if (!output) {
    result.error = "unable to write binary event log: " + path.string();
    return result;
  }

  const auto header = encode_header();
  output.write(reinterpret_cast<const char*>(header.data()),
               static_cast<std::streamsize>(header.size()));
  for (const MarketDataEvent& event : events) {
    const auto record = encode_record(event);
    output.write(reinterpret_cast<const char*>(record.data()),
                 static_cast<std::streamsize>(record.size()));
    result.event_checksum = append_to_checksum(result.event_checksum, event);
    ++result.events_written;
  }

  if (!output) {
    result.error = "failed while writing binary event log: " + path.string();
    result.events_written = 0;
    result.event_checksum = kFnvOffsetBasis;
  }
  return result;
}

} // namespace

std::string_view to_string(EventLogFormat format) noexcept {
  switch (format) {
  case EventLogFormat::Auto:
    return "auto";
  case EventLogFormat::Csv:
    return "csv";
  case EventLogFormat::Binary:
    return "binary";
  }
  return "unknown";
}

std::optional<EventLogFormat> parse_event_log_format(std::string_view value) {
  const std::string token = lower_copy(value);
  if (token == "auto") {
    return EventLogFormat::Auto;
  }
  if (token == "csv") {
    return EventLogFormat::Csv;
  }
  if (token == "binary" || token == "bin" || token == "itch") {
    return EventLogFormat::Binary;
  }
  return std::nullopt;
}

EventLogFormat choose_event_log_format_for_path(const std::filesystem::path& path) {
  const std::string extension = lower_copy(path.extension().string());
  if (extension == ".bin" || extension == ".itch" || extension == ".ablog") {
    return EventLogFormat::Binary;
  }
  return EventLogFormat::Csv;
}

EventLogFormat detect_event_log_format(const std::filesystem::path& path, std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error != nullptr) {
      *error = "unable to open event log: " + path.string();
    }
    return EventLogFormat::Auto;
  }

  std::array<char, kBinaryEventLogMagic.size()> magic{};
  input.read(magic.data(), static_cast<std::streamsize>(magic.size()));
  const std::streamsize bytes_read = input.gcount();
  if (bytes_read == static_cast<std::streamsize>(magic.size()) &&
      std::string_view(magic.data(), magic.size()) == kBinaryEventLogMagic) {
    return EventLogFormat::Binary;
  }
  return EventLogFormat::Csv;
}

std::uint64_t checksum_events(std::span<const MarketDataEvent> events) noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  for (const MarketDataEvent& event : events) {
    seed = append_to_checksum(seed, event);
  }
  return seed;
}

EventLogReadResult read_event_log(const std::filesystem::path& path, EventLogFormat format) {
  EventLogFormat effective_format = format;
  if (format == EventLogFormat::Auto) {
    std::string error;
    effective_format = detect_event_log_format(path, &error);
    if (!error.empty()) {
      EventLogReadResult result;
      result.error = error;
      return result;
    }
  }

  if (effective_format == EventLogFormat::Binary) {
    return read_binary_event_log(path);
  }
  return read_csv_event_log(path);
}

EventLogWriteResult write_event_log(const std::filesystem::path& path,
                                    std::span<const MarketDataEvent> events,
                                    EventLogFormat format) {
  EventLogFormat effective_format = format;
  if (format == EventLogFormat::Auto) {
    effective_format = choose_event_log_format_for_path(path);
  }

  if (effective_format == EventLogFormat::Binary) {
    return write_binary_event_log(path, events);
  }
  return write_csv_event_log(path, events);
}

} // namespace asterion
