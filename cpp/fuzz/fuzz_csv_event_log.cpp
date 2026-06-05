#include "fuzz_support.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

using namespace asterion;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  if (!fuzz::bounded_text(input)) {
    return 0;
  }

  const auto path = fuzz::write_temp_input("csv-event-log", ".csv", input);
  if (!path) {
    return 0;
  }

  const EventLogReadResult first = read_event_log(*path, EventLogFormat::Csv);
  const EventLogReadResult second = read_event_log(*path, EventLogFormat::Csv);
  fuzz::remove_temp_input(path);

  fuzz::require_same_parse_result(first, second);
  fuzz::require(first.events.size() <= fuzz::kMaxTextLines);
  if (first.error.empty()) {
    fuzz::require(first.event_checksum == checksum_events(first.events));
  }
  return 0;
}
