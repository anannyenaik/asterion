#include "asterion/market_data/synthetic_generator.hpp"

#include <algorithm>
#include <random>

namespace asterion {
namespace {

struct ActiveOrder {
  OrderId order_id{kInvalidOrderId};
  SymbolId symbol_id{kInvalidSymbolId};
  Side side{Side::None};
  PriceTicks price_ticks{0};
  Quantity quantity{0};
};

[[nodiscard]] Side random_side(std::mt19937& rng) {
  std::uniform_int_distribution<int> distribution(0, 1);
  return distribution(rng) == 0 ? Side::Buy : Side::Sell;
}

[[nodiscard]] SymbolId symbol_for_index(const SyntheticGeneratorConfig& config, std::size_t index) {
  const std::size_t symbol_count =
      config.mode == SyntheticFlowMode::MultiSymbol ? std::max<std::size_t>(1, config.symbol_count)
                                                    : 1;
  return config.symbol_id + static_cast<SymbolId>(index % symbol_count);
}

[[nodiscard]] TimestampNs timestamp_for_index(const SyntheticGeneratorConfig& config,
                                              std::size_t index) {
  if (config.mode != SyntheticFlowMode::BurstyFlow) {
    return config.first_timestamp_ns + static_cast<TimestampNs>(index);
  }
  const std::size_t burst_size = std::max<std::size_t>(1, config.burst_size);
  return config.first_timestamp_ns + static_cast<TimestampNs>(index / burst_size);
}

[[nodiscard]] PriceTicks price_range_for_mode(const SyntheticGeneratorConfig& config) {
  if (config.mode == SyntheticFlowMode::DeepBook) {
    return std::max<PriceTicks>(config.price_range_ticks, 100);
  }
  if (config.mode == SyntheticFlowMode::WidePriceRange) {
    return std::max<PriceTicks>(config.price_range_ticks, 500);
  }
  return std::max<PriceTicks>(config.price_range_ticks, 1);
}

[[nodiscard]] PriceTicks random_price(const SyntheticGeneratorConfig& config, Side side,
                                      std::mt19937& rng) {
  const PriceTicks range = price_range_for_mode(config);
  std::uniform_int_distribution<PriceTicks> depth_distribution(1, range);
  const PriceTicks depth = depth_distribution(rng);
  const PriceTicks price =
      side == Side::Buy ? config.mid_price_ticks - depth : config.mid_price_ticks + depth;
  return std::max<PriceTicks>(1, price);
}

[[nodiscard]] bool should_add(const SyntheticGeneratorConfig& config, int roll,
                              bool active_empty) {
  if (active_empty) {
    return true;
  }
  switch (config.mode) {
  case SyntheticFlowMode::HighCancellationRate:
    return roll < 35;
  case SyntheticFlowMode::ReplaceHeavy:
    return roll < 35;
  case SyntheticFlowMode::DeepBook:
    return roll < 75;
  case SyntheticFlowMode::LongRunningSameSymbol:
    return roll < 50;
  case SyntheticFlowMode::BurstyFlow:
  case SyntheticFlowMode::MultiSymbol:
  case SyntheticFlowMode::WidePriceRange:
  case SyntheticFlowMode::AdversarialLifecycle:
  case SyntheticFlowMode::Balanced:
    return roll < 55;
  }
  return roll < 55;
}

[[nodiscard]] MarketEventType choose_existing_order_event(const SyntheticGeneratorConfig& config,
                                                          int roll) {
  if (config.mode == SyntheticFlowMode::HighCancellationRate) {
    if (roll < 85) {
      return MarketEventType::Cancel;
    }
    return roll < 95 ? MarketEventType::Replace : MarketEventType::Execute;
  }
  if (config.mode == SyntheticFlowMode::ReplaceHeavy) {
    if (roll < 85) {
      return MarketEventType::Replace;
    }
    return roll < 95 ? MarketEventType::Cancel : MarketEventType::Execute;
  }
  if (roll < 75) {
    return MarketEventType::Cancel;
  }
  return roll < 90 ? MarketEventType::Replace : MarketEventType::Execute;
}

[[nodiscard]] std::vector<MarketDataEvent>
generate_adversarial_lifecycle_events(const SyntheticGeneratorConfig& config) {
  std::vector<MarketDataEvent> events;
  events.reserve(config.event_count);

  TradeId next_trade_id = 1;
  OrderId next_order_id = 1;
  std::size_t cycle = 0;

  const auto push = [&](MarketEventType type, Side side, PriceTicks price_ticks,
                        Quantity quantity, OrderId order_id, TradeId trade_id,
                        std::uint32_t flags = 0U) {
    if (events.size() >= config.event_count) {
      return;
    }
    const std::size_t index = events.size();
    events.push_back(MarketDataEvent{
        config.first_timestamp_ns + static_cast<TimestampNs>(index),
        config.first_sequence_number + static_cast<SequenceNumber>(index),
        config.symbol_id,
        type,
        side,
        price_ticks,
        quantity,
        order_id,
        trade_id,
        flags});
  };

  while (events.size() < config.event_count) {
    const OrderId buy_id = next_order_id++;
    const OrderId sell_id = next_order_id++;
    const PriceTicks bid =
        std::max<PriceTicks>(1, config.mid_price_ticks - 5 - static_cast<PriceTicks>(cycle % 17U));
    const PriceTicks ask =
        config.mid_price_ticks + 5 + static_cast<PriceTicks>(cycle % 17U);
    const PriceTicks bid_reuse = std::max<PriceTicks>(1, bid - 1);
    const PriceTicks bid_reprice = std::max<PriceTicks>(1, bid - 2);

    push(MarketEventType::Add, Side::Buy, bid, 100, buy_id, 0);
    push(MarketEventType::Cancel, Side::Buy, bid, 0, buy_id, 0);
    push(MarketEventType::Add, Side::Buy, bid_reuse, 80, buy_id, 0);
    push(MarketEventType::Replace, Side::Buy, bid_reuse, 120, buy_id, 0);
    push(MarketEventType::Replace, Side::Buy, bid_reuse, 60, buy_id, 0);
    push(MarketEventType::Replace, Side::Buy, bid_reprice, 70, buy_id, 0);
    push(MarketEventType::Cancel, Side::Buy, bid_reprice, 0, buy_id, 0);

    push(MarketEventType::Add, Side::Sell, ask, 90, sell_id, 0);
    push(MarketEventType::Execute, Side::Sell, ask, 30, sell_id, next_trade_id++);
    push(MarketEventType::Replace, Side::Sell, ask + 1, 50, sell_id, 0);
    push(MarketEventType::Execute, Side::Sell, ask + 1, 50, sell_id, next_trade_id++);
    push(MarketEventType::Add, Side::Sell, ask, 40, sell_id, 0);
    push(MarketEventType::Cancel, Side::Sell, ask, 20, sell_id, 0);
    push(MarketEventType::Cancel, Side::Sell, ask, 0, sell_id, 0);

    push(MarketEventType::Trade, Side::Buy, bid, 1, kInvalidOrderId, next_trade_id++);
    push(MarketEventType::Heartbeat, Side::None, 0, 0, kInvalidOrderId, 0);
    ++cycle;
  }

  return events;
}

} // namespace

std::vector<MarketDataEvent> generate_synthetic_events(const SyntheticGeneratorConfig& config) {
  if (config.mode == SyntheticFlowMode::AdversarialLifecycle) {
    return generate_adversarial_lifecycle_events(config);
  }

  std::mt19937 rng(config.seed);
  std::uniform_int_distribution<int> operation_distribution(0, 99);
  std::uniform_int_distribution<int> quantity_distribution(1, 250);

  std::vector<MarketDataEvent> events;
  events.reserve(config.event_count);
  std::vector<ActiveOrder> active_orders;
  active_orders.reserve(config.event_count);

  OrderId next_order_id = 1;
  TradeId next_trade_id = 1;
  for (std::size_t i = 0; i < config.event_count; ++i) {
    const SequenceNumber sequence_number =
        config.first_sequence_number + static_cast<SequenceNumber>(i);
    const TimestampNs timestamp_ns = timestamp_for_index(config, i);
    const int roll = operation_distribution(rng);

    if (should_add(config, roll, active_orders.empty())) {
      const Side side = random_side(rng);
      const SymbolId symbol_id = symbol_for_index(config, i);
      const PriceTicks price_ticks = random_price(config, side, rng);
      const Quantity quantity = quantity_distribution(rng);
      const OrderId order_id = next_order_id++;
      active_orders.push_back(ActiveOrder{order_id, symbol_id, side, price_ticks, quantity});
      events.push_back(MarketDataEvent{timestamp_ns, sequence_number, symbol_id,
                                       MarketEventType::Add, side, price_ticks, quantity, order_id,
                                       0, 0});
      continue;
    }

    std::uniform_int_distribution<std::size_t> active_distribution(std::size_t{0},
                                                                   active_orders.size() - 1U);
    const std::size_t active_index = active_distribution(rng);
    ActiveOrder& active = active_orders[active_index];
    const MarketEventType event_type = choose_existing_order_event(config, roll);

    if (event_type == MarketEventType::Cancel) {
      events.push_back(MarketDataEvent{timestamp_ns, sequence_number, active.symbol_id,
                                       MarketEventType::Cancel, active.side, active.price_ticks, 0,
                                       active.order_id, 0, 0});
      active_orders.erase(active_orders.begin() + static_cast<std::ptrdiff_t>(active_index));
      continue;
    }

    if (event_type == MarketEventType::Replace) {
      active.price_ticks = random_price(config, active.side, rng);
      active.quantity = quantity_distribution(rng);
      events.push_back(MarketDataEvent{timestamp_ns, sequence_number, active.symbol_id,
                                       MarketEventType::Replace, active.side, active.price_ticks,
                                       active.quantity, active.order_id, 0, 0});
      continue;
    }

    std::uniform_int_distribution<Quantity> execute_distribution(1, active.quantity);
    const Quantity executed_quantity = execute_distribution(rng);
    events.push_back(MarketDataEvent{timestamp_ns, sequence_number, active.symbol_id,
                                     MarketEventType::Execute, active.side, active.price_ticks,
                                     executed_quantity, active.order_id, next_trade_id++, 0});
    active.quantity -= executed_quantity;
    if (active.quantity == 0) {
      active_orders.erase(active_orders.begin() + static_cast<std::ptrdiff_t>(active_index));
    }
  }

  return events;
}

SimulatedMarketDataAdapter::SimulatedMarketDataAdapter(SyntheticGeneratorConfig config)
    : events_(generate_synthetic_events(config)) {}

std::optional<MarketDataEvent> SimulatedMarketDataAdapter::next() {
  if (cursor_ >= events_.size()) {
    return std::nullopt;
  }
  return events_[cursor_++];
}

std::vector<MarketDataEvent> SimulatedMarketDataAdapter::drain() {
  std::vector<MarketDataEvent> remaining;
  remaining.reserve(events_.size() - cursor_);
  while (auto event = next()) {
    remaining.push_back(*event);
  }
  return remaining;
}

} // namespace asterion
