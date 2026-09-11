#include "fuzz_support.hpp"

#include "asterion/risk/audit_manifest.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

using namespace asterion;

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  const std::span<const std::uint8_t> input(data, size);
  if (!fuzz::bounded_text(input)) {
    return 0;
  }

  const char* text_data = size == 0U ? "" : reinterpret_cast<const char*>(data);
  const std::string_view text(text_data, size);
  std::string first_error;
  std::string second_error;
  const auto first = parse_audit_manifest(text, &first_error);
  const auto second = parse_audit_manifest(text, &second_error);
  fuzz::require(first.has_value() == second.has_value());
  fuzz::require(first_error == second_error);

  if (first) {
    const std::string serialized = serialize_audit_manifest(*first);
    std::string round_trip_error;
    const auto round_trip = parse_audit_manifest(serialized, &round_trip_error);
    fuzz::require(round_trip.has_value());
    fuzz::require(round_trip_error.empty());
    fuzz::require(serialize_audit_manifest(*round_trip) == serialized);
    fuzz::require(canonical_audit_manifest_payload(*round_trip) ==
                  canonical_audit_manifest_payload(*first));
  }
  return 0;
}
