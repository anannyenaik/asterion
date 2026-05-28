#pragma once

#include "asterion/market_data/event.hpp"

#include <cstddef>
#include <vector>

namespace asterion {

struct SyntheticGeneratorConfig {
  SymbolId symbol_id{1};
  std::size_t event_count{100};
  SequenceNumber first_sequence_number{1};
  TimestampNs first_timestamp_ns{1'000'000};
  std::uint32_t seed{7};
};

[[nodiscard]] std::vector<MarketDataEvent>
generate_synthetic_events(const SyntheticGeneratorConfig& config);

} // namespace asterion
