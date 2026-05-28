#include "asterion/market_data/synthetic_generator.hpp"

#include <random>

namespace asterion {

std::vector<MarketDataEvent> generate_synthetic_events(const SyntheticGeneratorConfig& config) {
  std::mt19937 rng(config.seed);
  std::uniform_int_distribution<int> side_distribution(0, 1);
  std::uniform_int_distribution<int> price_offset(-5, 5);
  std::uniform_int_distribution<int> quantity_distribution(1, 250);

  std::vector<MarketDataEvent> events;
  events.reserve(config.event_count);

  OrderId next_order_id = 1;
  for (std::size_t i = 0; i < config.event_count; ++i) {
    const Side side = side_distribution(rng) == 0 ? Side::Buy : Side::Sell;
    const PriceTicks base_price = side == Side::Buy ? 999 : 1001;
    events.push_back(MarketDataEvent{config.first_timestamp_ns + static_cast<TimestampNs>(i),
                                     config.first_sequence_number + static_cast<SequenceNumber>(i),
                                     config.symbol_id,
                                     MarketEventType::Add,
                                     side,
                                     base_price + price_offset(rng),
                                     quantity_distribution(rng),
                                     next_order_id++,
                                     0,
                                     0});
  }

  return events;
}

} // namespace asterion
