#include "fuzz_support.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

using namespace asterion;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size > fuzz::kMaxParserInputSize) {
    return 0;
  }

  const std::span<const std::uint8_t> input(data, size);
  const auto path = fuzz::write_temp_input("binary-event-log", ".bin", input);
  if (!path) {
    return 0;
  }

  const EventLogReadResult first = read_event_log(*path, EventLogFormat::Binary);
  const EventLogReadResult second = read_event_log(*path, EventLogFormat::Binary);
  fuzz::remove_temp_input(path);

  fuzz::require_same_parse_result(first, second);
  fuzz::require(first.events.size() <= (fuzz::kMaxParserInputSize / kBinaryEventRecordSize) + 1U);
  if (first.error.empty()) {
    fuzz::require(first.event_checksum == checksum_events(first.events));
  }
  return 0;
}
