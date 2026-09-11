#pragma once

#include "asterion/market_data/event.hpp"

#include <cstddef>
#include <optional>
#include <vector>

namespace asterion {

enum class SyntheticFlowMode {
  Balanced,
  HighCancellationRate,
  ReplaceHeavy,
  DeepBook,
  BurstyFlow,
  LongRunningSameSymbol,
  MultiSymbol,
  WidePriceRange,
  AdversarialLifecycle
};

struct SyntheticGeneratorConfig {
  SymbolId symbol_id{1};
  std::size_t event_count{100};
  SequenceNumber first_sequence_number{1};
  TimestampNs first_timestamp_ns{1'000'000};
  std::uint32_t seed{7};
  SyntheticFlowMode mode{SyntheticFlowMode::Balanced};
  std::size_t symbol_count{1};
  PriceTicks mid_price_ticks{1000};
  PriceTicks price_range_ticks{5};
  std::size_t burst_size{8};
};

[[nodiscard]] std::vector<MarketDataEvent>
generate_synthetic_events(const SyntheticGeneratorConfig& config);

class SimulatedMarketDataAdapter {
public:
  explicit SimulatedMarketDataAdapter(SyntheticGeneratorConfig config);

  [[nodiscard]] std::optional<MarketDataEvent> next();
  [[nodiscard]] std::vector<MarketDataEvent> drain();

private:
  std::vector<MarketDataEvent> events_;
  std::size_t cursor_{0};
};

} // namespace asterion
