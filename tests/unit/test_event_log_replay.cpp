#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/market_data/replay_aggregate.hpp"
#include "asterion/market_data/synthetic_generator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <type_traits>
#include <vector>

using namespace asterion;

namespace {

constexpr std::string_view kEventLogSchemaGuardHint =
    "If this event-log schema drift is intentional, update "
    "data/schema/event_log_schema_v1.json and docs/event_log_schema.md with the migration note, "
    "then regenerate affected fixtures.";

std::filesystem::path temp_path(std::string_view suffix) {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  return std::filesystem::temp_directory_path() /
         ("asterion_" + std::to_string(stamp) + std::string(suffix));
}

std::vector<MarketDataEvent> representative_events() {
  return {
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 100, 10, 0, 0},
      MarketDataEvent{2, 2, 1, MarketEventType::Replace, Side::Buy, 998, 80, 10, 0, 1},
      MarketDataEvent{3, 3, 1, MarketEventType::Execute, Side::Buy, 998, 30, 10, 700, 0},
      MarketDataEvent{4, 4, 1, MarketEventType::Trade, Side::None, 1000, 5, 0, 701, 2},
      MarketDataEvent{5, 5, 1, MarketEventType::Cancel, Side::Buy, 998, 0, 10, 0, 0},
      MarketDataEvent{6, 6, 1, MarketEventType::Snapshot, Side::None, 0, 0, 0, 0, 0},
      MarketDataEvent{7, 7, 1, MarketEventType::Heartbeat, Side::None, 0, 0, 0, 0, 0},
  };
}

std::string fingerprint(const std::vector<MarketDataEvent>& events) {
  std::string output;
  for (const MarketDataEvent& event : events) {
    output += market_data_event_to_csv(event);
    output += '\n';
  }
  return output;
}

void require_matching_replay(const std::vector<MarketDataEvent>& events) {
  const auto csv_path = temp_path(".csv");
  const auto binary_path = temp_path(".bin");

  const EventLogWriteResult csv_write = write_event_log(csv_path, events, EventLogFormat::Csv);
  const EventLogWriteResult binary_write =
      write_event_log(binary_path, events, EventLogFormat::Binary);
  REQUIRE(csv_write.error.empty());
  REQUIRE(binary_write.error.empty());

  ReplayEngine csv_replay(1);
  ReplayEngine binary_replay(1);
  const ReplayResult csv_result = csv_replay.replay_file(csv_path, EventLogFormat::Csv);
  const ReplayResult binary_result = binary_replay.replay_file(binary_path, EventLogFormat::Binary);

  INFO(csv_result.error);
  INFO(binary_result.error);
  REQUIRE(csv_result.sequence_valid == binary_result.sequence_valid);
  REQUIRE(csv_result.event_log_checksum == binary_result.event_log_checksum);
  REQUIRE(csv_result.final_book_checksum == binary_result.final_book_checksum);
  REQUIRE(csv_result.execution_report_checksum == binary_result.execution_report_checksum);
  REQUIRE(csv_result.diagnostics_checksum == binary_result.diagnostics_checksum);
  REQUIRE(csv_result.diagnostic_error_count == binary_result.diagnostic_error_count);

  std::filesystem::remove(csv_path);
  std::filesystem::remove(binary_path);
}

void write_text(const std::filesystem::path& path, std::string_view content) {
  std::ofstream output(path);
  output << content;
}

std::vector<std::uint8_t> read_binary_bytes(const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

template <typename T>
[[nodiscard]] T read_little_endian_from(std::span<const std::uint8_t> bytes, std::size_t offset) {
  static_assert(std::is_integral_v<T>);
  std::uint64_t value = 0;
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    value |= static_cast<std::uint64_t>(bytes[offset + i]) << (i * 8U);
  }
  return static_cast<T>(value);
}

} // namespace

TEST_CASE("Event-log schema constants match the documented v1 wire contract",
          "[event-log][schema]") {
  INFO(kEventLogSchemaGuardHint);
  REQUIRE(kMarketDataCsvHeader ==
          "timestamp_ns,sequence_number,symbol_id,event_type,side,price_ticks,quantity,"
          "order_id,trade_id,flags");
  REQUIRE(kBinaryEventLogMagic == "ASTITCH1");
  REQUIRE(kBinaryEventLogVersion == 1);
  REQUIRE(kBinaryEventLogHeaderSize == 16);
  REQUIRE(kBinaryEventRecordSize == 58);

  REQUIRE(static_cast<std::uint8_t>(MarketEventType::Add) == 1);
  REQUIRE(static_cast<std::uint8_t>(MarketEventType::Cancel) == 2);
  REQUIRE(static_cast<std::uint8_t>(MarketEventType::Replace) == 3);
  REQUIRE(static_cast<std::uint8_t>(MarketEventType::Execute) == 4);
  REQUIRE(static_cast<std::uint8_t>(MarketEventType::Trade) == 5);
  REQUIRE(static_cast<std::uint8_t>(MarketEventType::Snapshot) == 6);
  REQUIRE(static_cast<std::uint8_t>(MarketEventType::Heartbeat) == 7);

  REQUIRE(static_cast<std::uint8_t>(Side::None) == 0);
  REQUIRE(static_cast<std::uint8_t>(Side::Buy) == 1);
  REQUIRE(static_cast<std::uint8_t>(Side::Sell) == 2);

  REQUIRE(kSnapshotBeginFlag == 0x1U);
  REQUIRE(kSnapshotEndFlag == 0x2U);
}

TEST_CASE("Binary event-log writer preserves v1 header and record field layout",
          "[event-log][schema][binary]") {
  INFO(kEventLogSchemaGuardHint);
  const MarketDataEvent event{0x0102030405060708LL,
                              0x1112131415161718ULL,
                              0x21222324U,
                              MarketEventType::Trade,
                              Side::Sell,
                              0x0414243444546474LL,
                              0x0515253545556575LL,
                              0x6162636465666768ULL,
                              0x7172737475767778ULL,
                              0x31323334U};
  const auto path = temp_path(".bin");
  REQUIRE(write_event_log(path, std::span<const MarketDataEvent>(&event, 1), EventLogFormat::Binary)
              .error.empty());

  const std::vector<std::uint8_t> bytes = read_binary_bytes(path);
  REQUIRE(bytes.size() == kBinaryEventLogHeaderSize + kBinaryEventRecordSize);
  REQUIRE(std::string_view(reinterpret_cast<const char*>(bytes.data()), kBinaryEventLogMagic.size()) ==
          kBinaryEventLogMagic);
  REQUIRE(read_little_endian_from<std::uint16_t>(bytes, 8) == kBinaryEventLogVersion);
  REQUIRE(read_little_endian_from<std::uint16_t>(bytes, 10) == kBinaryEventLogHeaderSize);
  REQUIRE(read_little_endian_from<std::uint16_t>(bytes, 12) == kBinaryEventRecordSize);
  REQUIRE(read_little_endian_from<std::uint16_t>(bytes, 14) == 0U);

  const std::span<const std::uint8_t> record(bytes.data() + kBinaryEventLogHeaderSize,
                                             kBinaryEventRecordSize);
  REQUIRE(read_little_endian_from<TimestampNs>(record, 0) == event.timestamp_ns);
  REQUIRE(read_little_endian_from<SequenceNumber>(record, 8) == event.sequence_number);
  REQUIRE(read_little_endian_from<SymbolId>(record, 16) == event.symbol_id);
  REQUIRE(read_little_endian_from<std::uint8_t>(record, 20) ==
          static_cast<std::uint8_t>(event.event_type));
  REQUIRE(read_little_endian_from<std::uint8_t>(record, 21) ==
          static_cast<std::uint8_t>(event.side));
  REQUIRE(read_little_endian_from<std::uint32_t>(record, 22) == event.flags);
  REQUIRE(read_little_endian_from<PriceTicks>(record, 26) == event.price_ticks);
  REQUIRE(read_little_endian_from<Quantity>(record, 34) == event.quantity);
  REQUIRE(read_little_endian_from<OrderId>(record, 42) == event.order_id);
  REQUIRE(read_little_endian_from<TradeId>(record, 50) == event.trade_id);

  std::filesystem::remove(path);
}

TEST_CASE("Binary event log round-trips every event kind", "[event-log][binary]") {
  const std::vector<MarketDataEvent> events = representative_events();
  const auto path = temp_path(".bin");

  const EventLogWriteResult write = write_event_log(path, events, EventLogFormat::Binary);
  REQUIRE(write.error.empty());
  REQUIRE(write.events_written == events.size());
  REQUIRE(write.event_checksum == checksum_events(events));

  const EventLogReadResult read = read_event_log(path, EventLogFormat::Auto);
  REQUIRE(read.error.empty());
  REQUIRE(read.detected_format == EventLogFormat::Binary);
  REQUIRE(read.event_checksum == checksum_events(events));
  REQUIRE(fingerprint(read.events) == fingerprint(events));

  std::filesystem::remove(path);
}

TEST_CASE("CSV and binary event logs preserve ordering and replay checksums",
          "[event-log][replay]") {
  require_matching_replay(representative_events());
}

TEST_CASE("Malformed binary event logs are rejected safely", "[event-log][binary][adversarial]") {
  const auto truncated_header_path = temp_path(".bin");
  write_text(truncated_header_path, "AST");
  const EventLogReadResult truncated_header =
      read_event_log(truncated_header_path, EventLogFormat::Binary);
  REQUIRE_FALSE(truncated_header.error.empty());
  REQUIRE(truncated_header.error.find("truncated binary event-log header") != std::string::npos);
  REQUIRE(truncated_header.events.empty());
  std::filesystem::remove(truncated_header_path);

  const auto invalid_magic_path = temp_path(".bin");
  write_text(invalid_magic_path, "BADITCH1XXXXXXXX");
  const EventLogReadResult invalid_magic =
      read_event_log(invalid_magic_path, EventLogFormat::Binary);
  REQUIRE_FALSE(invalid_magic.error.empty());
  REQUIRE(invalid_magic.error.find("magic") != std::string::npos);
  REQUIRE(invalid_magic.events.empty());
  std::filesystem::remove(invalid_magic_path);

  const auto invalid_version_path = temp_path(".bin");
  REQUIRE(write_event_log(invalid_version_path, representative_events(), EventLogFormat::Binary)
              .error.empty());
  std::fstream invalid_version(invalid_version_path, std::ios::binary | std::ios::in |
                                                         std::ios::out);
  invalid_version.seekp(8);
  const char unsupported_version[] = {static_cast<char>(99), 0};
  invalid_version.write(unsupported_version, 2);
  invalid_version.close();
  const EventLogReadResult version = read_event_log(invalid_version_path, EventLogFormat::Binary);
  REQUIRE_FALSE(version.error.empty());
  REQUIRE(version.error.find("version") != std::string::npos);
  REQUIRE(version.events.empty());
  std::filesystem::remove(invalid_version_path);

  const auto invalid_record_size_path = temp_path(".bin");
  REQUIRE(write_event_log(invalid_record_size_path, representative_events(), EventLogFormat::Binary)
              .error.empty());
  std::fstream invalid_record_size(invalid_record_size_path,
                                   std::ios::binary | std::ios::in | std::ios::out);
  invalid_record_size.seekp(12);
  const char bad_record_size[] = {static_cast<char>(1), 0};
  invalid_record_size.write(bad_record_size, 2);
  invalid_record_size.close();
  const EventLogReadResult record_size =
      read_event_log(invalid_record_size_path, EventLogFormat::Binary);
  REQUIRE_FALSE(record_size.error.empty());
  REQUIRE(record_size.error.find("record size") != std::string::npos);
  REQUIRE(record_size.events.empty());
  std::filesystem::remove(invalid_record_size_path);

  const auto invalid_header_size_path = temp_path(".bin");
  REQUIRE(write_event_log(invalid_header_size_path, representative_events(), EventLogFormat::Binary)
              .error.empty());
  std::fstream invalid_header_size(invalid_header_size_path,
                                   std::ios::binary | std::ios::in | std::ios::out);
  invalid_header_size.seekp(10);
  const char bad_header_size[] = {static_cast<char>(1), 0};
  invalid_header_size.write(bad_header_size, 2);
  invalid_header_size.close();
  const EventLogReadResult header_size =
      read_event_log(invalid_header_size_path, EventLogFormat::Binary);
  REQUIRE_FALSE(header_size.error.empty());
  REQUIRE(header_size.error.find("header size") != std::string::npos);
  REQUIRE(header_size.events.empty());
  std::filesystem::remove(invalid_header_size_path);

  const auto invalid_reserved_path = temp_path(".bin");
  REQUIRE(write_event_log(invalid_reserved_path, representative_events(), EventLogFormat::Binary)
              .error.empty());
  std::fstream invalid_reserved(invalid_reserved_path,
                                std::ios::binary | std::ios::in | std::ios::out);
  invalid_reserved.seekp(14);
  const char bad_reserved[] = {static_cast<char>(1), 0};
  invalid_reserved.write(bad_reserved, 2);
  invalid_reserved.close();
  const EventLogReadResult reserved = read_event_log(invalid_reserved_path, EventLogFormat::Binary);
  REQUIRE_FALSE(reserved.error.empty());
  REQUIRE(reserved.error.find("reserved header field") != std::string::npos);
  REQUIRE(reserved.events.empty());
  std::filesystem::remove(invalid_reserved_path);

  const auto truncated_path = temp_path(".bin");
  REQUIRE(write_event_log(truncated_path, representative_events(), EventLogFormat::Binary)
              .error.empty());
  std::filesystem::resize_file(truncated_path, kBinaryEventLogHeaderSize + 3U);

  const EventLogReadResult truncated = read_event_log(truncated_path, EventLogFormat::Binary);
  REQUIRE_FALSE(truncated.error.empty());
  REQUIRE(truncated.error.find("truncated") != std::string::npos);
  REQUIRE(truncated.events.empty());
  std::filesystem::remove(truncated_path);

  const auto invalid_type_path = temp_path(".bin");
  REQUIRE(write_event_log(invalid_type_path, representative_events(), EventLogFormat::Binary)
              .error.empty());
  std::fstream invalid_type(invalid_type_path, std::ios::binary | std::ios::in | std::ios::out);
  invalid_type.seekp(static_cast<std::streamoff>(kBinaryEventLogHeaderSize + 20U));
  const char invalid_event_type = static_cast<char>(99);
  invalid_type.write(&invalid_event_type, 1);
  invalid_type.close();

  const EventLogReadResult invalid = read_event_log(invalid_type_path, EventLogFormat::Binary);
  REQUIRE_FALSE(invalid.error.empty());
  REQUIRE(invalid.error.find("enum drift") != std::string::npos);
  REQUIRE(invalid.error.find("MarketEventType") != std::string::npos);
  REQUIRE(invalid.events.empty());
  std::filesystem::remove(invalid_type_path);

  const auto invalid_side_path = temp_path(".bin");
  REQUIRE(write_event_log(invalid_side_path, representative_events(), EventLogFormat::Binary)
              .error.empty());
  std::fstream invalid_side(invalid_side_path, std::ios::binary | std::ios::in | std::ios::out);
  invalid_side.seekp(static_cast<std::streamoff>(kBinaryEventLogHeaderSize + 21U));
  const char invalid_side_value = static_cast<char>(99);
  invalid_side.write(&invalid_side_value, 1);
  invalid_side.close();

  const EventLogReadResult side = read_event_log(invalid_side_path, EventLogFormat::Binary);
  REQUIRE_FALSE(side.error.empty());
  REQUIRE(side.error.find("enum drift") != std::string::npos);
  REQUIRE(side.error.find("Side") != std::string::npos);
  REQUIRE(side.events.empty());
  std::filesystem::remove(invalid_side_path);
}

TEST_CASE("Malformed CSV event logs are rejected safely", "[event-log][csv][adversarial]") {
  const auto invalid_header_path = temp_path(".csv");
  write_text(invalid_header_path, "not,a,valid,header\n");
  const EventLogReadResult invalid_header =
      read_event_log(invalid_header_path, EventLogFormat::Csv);
  REQUIRE_FALSE(invalid_header.error.empty());
  REQUIRE(invalid_header.error.find("CSV column drift") != std::string::npos);
  REQUIRE(invalid_header.events.empty());
  std::filesystem::remove(invalid_header_path);

  const auto reordered_header_path = temp_path(".csv");
  write_text(reordered_header_path,
             "event_type,timestamp_ns,sequence_number,symbol_id,side,price_ticks,quantity,"
             "order_id,trade_id,flags\n");
  const EventLogReadResult reordered_header =
      read_event_log(reordered_header_path, EventLogFormat::Csv);
  REQUIRE_FALSE(reordered_header.error.empty());
  REQUIRE(reordered_header.error.find("CSV column drift") != std::string::npos);
  REQUIRE(reordered_header.events.empty());
  std::filesystem::remove(reordered_header_path);

  const auto invalid_enum_path = temp_path(".csv");
  write_text(invalid_enum_path,
             std::string(kMarketDataCsvHeader) +
                 "\n1,1,1,Bogus,Buy,1000,10,1,0,0\n");
  const EventLogReadResult invalid_enum =
      read_event_log(invalid_enum_path, EventLogFormat::Csv);
  REQUIRE_FALSE(invalid_enum.error.empty());
  REQUIRE(invalid_enum.error.find("failed to parse") != std::string::npos);
  REQUIRE(invalid_enum.events.empty());
  std::filesystem::remove(invalid_enum_path);

  const auto oversized_path = temp_path(".csv");
  write_text(oversized_path,
             std::string(kMarketDataCsvHeader) +
                 "\n999999999999999999999999999999,1,1,Add,Buy,1000,10,1,0,0\n");
  const EventLogReadResult oversized = read_event_log(oversized_path, EventLogFormat::Csv);
  REQUIRE_FALSE(oversized.error.empty());
  REQUIRE(oversized.error.find("failed to parse") != std::string::npos);
  REQUIRE(oversized.error.find("timestamp_ns") != std::string::npos);
  REQUIRE(oversized.events.empty());
  std::filesystem::remove(oversized_path);
}

TEST_CASE("Replay diagnostics report malformed event streams", "[replay][diagnostics]") {
  const std::vector<MarketDataEvent> duplicate_order_id{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 1, MarketEventType::Add, Side::Buy, 998, 10, 1, 0, 0},
  };
  ReplayEngine duplicate_replay(1);
  const ReplayResult duplicate_result = duplicate_replay.replay_events(duplicate_order_id);
  REQUIRE_FALSE(duplicate_result.sequence_valid);
  REQUIRE(duplicate_result.diagnostic_error_count == 1);
  REQUIRE(duplicate_result.diagnostics.front().event_index == 1);
  REQUIRE(duplicate_result.diagnostics.front().reason.find("duplicate order id") !=
          std::string::npos);

  const std::vector<MarketDataEvent> invalid_price{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Sell, 0, 10, 1, 0, 0},
  };
  ReplayEngine price_replay(1);
  const ReplayResult price_result = price_replay.replay_events(invalid_price);
  REQUIRE_FALSE(price_result.sequence_valid);
  REQUIRE(price_result.diagnostics.front().reason.find("invalid price") != std::string::npos);

  const std::vector<MarketDataEvent> crossed_book{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 1001, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 1, MarketEventType::Add, Side::Sell, 1000, 10, 2, 0, 0},
  };
  ReplayEngine crossed_replay(1);
  const ReplayResult crossed_result = crossed_replay.replay_events(crossed_book);
  REQUIRE_FALSE(crossed_result.sequence_valid);
  REQUIRE(crossed_result.diagnostics.front().reason.find("crossed book") != std::string::npos);
}

TEST_CASE("Replay diagnostics cover broader malformed feed properties",
          "[replay][diagnostics][adversarial]") {
  struct Case {
    std::string_view name;
    std::vector<MarketDataEvent> events;
    std::string_view expected_reason;
  };

  const std::vector<Case> cases{
      Case{"zero-quantity",
           {MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 1000, 0, 1, 0, 0}},
           "invalid quantity"},
      Case{"negative-quantity",
           {MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 1000, -1, 1, 0, 0}},
           "invalid quantity"},
      Case{"invalid-price",
           {MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, -1, 10, 1, 0, 0}},
           "invalid price"},
      Case{"sequence-gap",
           {MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 1000, 10, 1, 0, 0},
            MarketDataEvent{2, 3, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 1, 0, 0}},
           "sequence gap"},
      Case{"timestamp-reversal",
           {MarketDataEvent{2, 1, 1, MarketEventType::Add, Side::Buy, 1000, 10, 1, 0, 0},
            MarketDataEvent{1, 2, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 1, 0, 0}},
           "timestamp reversal"},
      Case{"snapshot-misuse",
           {MarketDataEvent{1, 1, 1, MarketEventType::Snapshot, Side::None, 1000, 10, 1, 0, 0}},
           "invalid snapshot side"},
      Case{"over-reduction",
           {MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 1000, 10, 1, 0, 0},
            MarketDataEvent{2, 2, 1, MarketEventType::Execute, Side::Buy, 1000, 11, 1, 7, 0}},
           "exceeds resting order quantity"},
      Case{"unknown-cancel",
           {MarketDataEvent{1, 1, 1, MarketEventType::Cancel, Side::Buy, 0, 0, 42, 0, 0}},
           "unknown cancel"},
      Case{"unknown-replace",
           {MarketDataEvent{1, 1, 1, MarketEventType::Replace, Side::Buy, 1000, 10, 42, 0, 0}},
           "unknown replace"},
  };

  for (const Case& test_case : cases) {
    INFO(test_case.name);
    ReplayEngine replay(1);
    const ReplayResult result = replay.replay_events(test_case.events);
    REQUIRE_FALSE(result.sequence_valid);
    REQUIRE(result.diagnostic_error_count == 1);
    REQUIRE_FALSE(result.diagnostics.empty());
    REQUIRE(result.diagnostics.front().reason.find(test_case.expected_reason) !=
            std::string::npos);
  }
}

TEST_CASE("Replay checksums are stable for the sample stream", "[replay][checksum]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Sell, 1001, 100, 11, 0, 0},
      MarketDataEvent{2, 2, 1, MarketEventType::Add, Side::Sell, 1002, 50, 12, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Execute, Side::Sell, 1001, 100, 11, 77, 0},
  };

  ReplayEngine replay_a(1);
  ReplayEngine replay_b(1);
  const ReplayResult result_a = replay_a.replay_events(events);
  const ReplayResult result_b = replay_b.replay_events(events);

  REQUIRE(result_a.error.empty());
  REQUIRE(result_a.sequence_valid);
  REQUIRE(result_a.final_book_checksum == result_b.final_book_checksum);
  REQUIRE(result_a.execution_report_checksum == result_b.execution_report_checksum);
  REQUIRE(result_a.diagnostics_checksum == result_b.diagnostics_checksum);
  REQUIRE(result_a.event_log_checksum == checksum_events(events));
  REQUIRE(result_a.event_log_checksum == 6081551686519738934ULL);
  REQUIRE(result_a.final_book_checksum == 8911332365672283169ULL);
  REQUIRE(result_a.execution_report_checksum == 4737330456958314376ULL);
  REQUIRE(result_a.diagnostics_checksum == 14695981039346656037ULL);
}

TEST_CASE("Aggregate replay summarizes global multi-symbol streams by symbol",
          "[replay][aggregate]") {
  const std::vector<MarketDataEvent> events{
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 10, 1, 0, 0},
      MarketDataEvent{2, 2, 2, MarketEventType::Add, Side::Sell, 1001, 5, 2, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Cancel, Side::Buy, 999, 0, 1, 0, 0},
      MarketDataEvent{4, 4, 2, MarketEventType::Execute, Side::Sell, 1001, 3, 2, 77, 0},
  };

  const AggregateReplaySummary aggregate = replay_by_symbol(events);
  REQUIRE(aggregate.error.empty());
  REQUIRE(aggregate.total_events == events.size());
  REQUIRE(aggregate.symbol_count == 2);
  REQUIRE(aggregate.symbols.size() == 2);

  const SymbolReplaySummary& symbol_one = aggregate.symbols[0];
  REQUIRE(symbol_one.symbol_id == 1);
  REQUIRE(symbol_one.event_count == 2);
  REQUIRE(symbol_one.first_sequence == 1);
  REQUIRE(symbol_one.last_sequence == 3);
  REQUIRE(symbol_one.sequence_valid);
  REQUIRE(symbol_one.diagnostic_count == 0);

  const SymbolReplaySummary& symbol_two = aggregate.symbols[1];
  REQUIRE(symbol_two.symbol_id == 2);
  REQUIRE(symbol_two.event_count == 2);
  REQUIRE(symbol_two.first_sequence == 2);
  REQUIRE(symbol_two.last_sequence == 4);
  REQUIRE(symbol_two.sequence_valid);
  REQUIRE(symbol_two.execution_report_checksum != kFnvOffsetBasis);

  ReplayConfig replay_config;
  replay_config.validate_sequence_numbers = false;
  ReplayEngine replay_two(2, replay_config);
  const std::vector<MarketDataEvent> symbol_two_events{events[1], events[3]};
  const ReplayResult replay_two_result = replay_two.replay_events(symbol_two_events);
  REQUIRE(symbol_two.final_book_checksum == replay_two_result.final_book_checksum);
  REQUIRE(symbol_two.diagnostics_checksum == replay_two_result.diagnostics_checksum);

  AggregateReplayConfig strict_config;
  strict_config.validate_per_symbol_sequences = true;
  const AggregateReplaySummary strict = replay_by_symbol(events, strict_config);
  REQUIRE(strict.symbols[0].diagnostic_error_count == 1);
  REQUIRE(strict.symbols[0].diagnostics.front().reason.find("sequence gap") !=
          std::string::npos);
}

TEST_CASE("Generated streams replay identically from CSV and binary logs",
          "[property][event-log][replay]") {
  const std::vector<SyntheticFlowMode> modes{
      SyntheticFlowMode::Balanced, SyntheticFlowMode::HighCancellationRate,
      SyntheticFlowMode::ReplaceHeavy, SyntheticFlowMode::DeepBook,
      SyntheticFlowMode::BurstyFlow, SyntheticFlowMode::LongRunningSameSymbol,
      SyntheticFlowMode::WidePriceRange, SyntheticFlowMode::AdversarialLifecycle};

  for (const SyntheticFlowMode mode : modes) {
    SyntheticGeneratorConfig config;
    config.event_count = 200;
    config.seed = 2026;
    config.mode = mode;
    require_matching_replay(generate_synthetic_events(config));
  }
}
