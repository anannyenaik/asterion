#pragma once

#include "asterion/market_data/event.hpp"
#include "asterion/market_data/event_log.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <optional>
#include <span>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <process.h>
#else
#include <unistd.h>
#endif

namespace asterion::fuzz {

inline constexpr std::size_t kMaxParserInputSize = 64U * 1024U;
inline constexpr std::size_t kMaxTextLines = 512U;
inline constexpr std::size_t kMaxTextLineSize = 4096U;
inline constexpr std::size_t kMaxGeneratedEvents = 128U;
inline constexpr std::size_t kMaxMatchingInputSize = 4096U;
inline constexpr std::size_t kMaxMatchingOperations = 128U;

[[noreturn]] inline void fail() {
  std::abort();
}

inline void require(bool condition) {
  if (!condition) {
    fail();
  }
}

[[nodiscard]] inline bool bounded_text(std::span<const std::uint8_t> input) {
  if (input.size() > kMaxParserInputSize) {
    return false;
  }
  std::size_t line_count = 1;
  std::size_t line_size = 0;
  for (const std::uint8_t byte : input) {
    if (byte == static_cast<std::uint8_t>('\n')) {
      ++line_count;
      line_size = 0;
    } else {
      ++line_size;
    }
    if (line_count > kMaxTextLines || line_size > kMaxTextLineSize) {
      return false;
    }
  }
  return true;
}

[[nodiscard]] inline std::optional<std::filesystem::path>
write_temp_input(std::string_view target, std::string_view extension,
                 std::span<const std::uint8_t> input) {
#if defined(_WIN32)
  const int process_id = _getpid();
#else
  const int process_id = getpid();
#endif
  std::filesystem::path path;
  try {
    path = std::filesystem::temp_directory_path() /
           ("asterion-" + std::string(target) + "-" + std::to_string(process_id) +
            std::string(extension));
  } catch (const std::filesystem::filesystem_error&) {
    return std::nullopt;
  }

  std::ofstream output(path, std::ios::binary | std::ios::trunc);
  if (!output) {
    return std::nullopt;
  }
  if (!input.empty()) {
    output.write(reinterpret_cast<const char*>(input.data()),
                 static_cast<std::streamsize>(input.size()));
  }
  if (!output) {
    return std::nullopt;
  }
  return path;
}

inline void remove_temp_input(const std::optional<std::filesystem::path>& path) {
  if (!path) {
    return;
  }
  std::error_code error;
  std::filesystem::remove(*path, error);
}

inline void require_same_parse_result(const EventLogReadResult& first,
                                      const EventLogReadResult& second) {
  require(first.detected_format == second.detected_format);
  require(first.error == second.error);
  require(first.event_checksum == second.event_checksum);
  require(first.events.size() == second.events.size());
  require(checksum_events(first.events) == checksum_events(second.events));
}

class ByteReader {
public:
  explicit ByteReader(std::span<const std::uint8_t> bytes) : bytes_(bytes) {}

  [[nodiscard]] std::uint8_t next() {
    if (bytes_.empty()) {
      return 0;
    }
    const std::uint8_t value = bytes_[position_ % bytes_.size()];
    ++position_;
    return value;
  }

private:
  std::span<const std::uint8_t> bytes_;
  std::size_t position_{0};
};

[[nodiscard]] inline Side side_from_byte(std::uint8_t byte) {
  switch (byte % 3U) {
  case 0:
    return Side::Buy;
  case 1:
    return Side::Sell;
  default:
    return Side::None;
  }
}

} // namespace asterion::fuzz
