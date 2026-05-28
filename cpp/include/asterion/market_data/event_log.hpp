#pragma once

#include "asterion/market_data/event.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace asterion {

enum class EventLogFormat { Auto, Csv, Binary };

inline constexpr std::string_view kMarketDataCsvHeader =
    "timestamp_ns,sequence_number,symbol_id,event_type,side,price_ticks,quantity,order_id,"
    "trade_id,flags";

inline constexpr std::string_view kBinaryEventLogMagic = "ASTITCH1";
inline constexpr std::uint16_t kBinaryEventLogVersion = 1;
inline constexpr std::uint16_t kBinaryEventLogHeaderSize = 16;
inline constexpr std::uint16_t kBinaryEventRecordSize = 58;

struct EventLogReadResult {
  std::vector<MarketDataEvent> events;
  EventLogFormat detected_format{EventLogFormat::Auto};
  std::uint64_t event_checksum{0};
  std::string error;
};

struct EventLogWriteResult {
  std::size_t events_written{0};
  std::uint64_t event_checksum{0};
  std::string error;
};

[[nodiscard]] std::string_view to_string(EventLogFormat format) noexcept;
[[nodiscard]] std::optional<EventLogFormat> parse_event_log_format(std::string_view value);
[[nodiscard]] EventLogFormat choose_event_log_format_for_path(const std::filesystem::path& path);
[[nodiscard]] EventLogFormat detect_event_log_format(const std::filesystem::path& path,
                                                     std::string* error);
[[nodiscard]] std::uint64_t checksum_events(std::span<const MarketDataEvent> events) noexcept;

[[nodiscard]] EventLogReadResult read_event_log(const std::filesystem::path& path,
                                                EventLogFormat format = EventLogFormat::Auto);
[[nodiscard]] EventLogWriteResult write_event_log(const std::filesystem::path& path,
                                                  std::span<const MarketDataEvent> events,
                                                  EventLogFormat format);

} // namespace asterion
