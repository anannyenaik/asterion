#include "asterion/book/order_book.hpp"

#include "asterion/core/checksum.hpp"

#include <sstream>
#include <unordered_set>

namespace asterion {

namespace {

void add_violation(BookInvariantReport& report, std::string message) {
  report.ok = false;
  report.violations.push_back(std::move(message));
}

template <typename Levels>
std::uint64_t checksum_levels(std::uint64_t seed, Side side, const Levels& levels) {
  seed = checksum_append(seed, side);
  seed = checksum_append(seed, static_cast<std::uint64_t>(levels.size()));
  for (const auto& [price, level] : levels) {
    seed = checksum_append(seed, price);
    seed = checksum_append(seed, level.total_quantity);
    seed = checksum_append(seed, static_cast<std::uint64_t>(level.orders.size()));
    for (const auto& order : level.orders) {
      seed = checksum_append(seed, order.order_id);
      seed = checksum_append(seed, order.client_order_id);
      seed = checksum_append(seed, order.symbol_id);
      seed = checksum_append(seed, order.quantity);
      seed = checksum_append(seed, order.timestamp_ns);
      seed = checksum_append(seed, order.sequence_number);
    }
  }
  return seed;
}

} // namespace

OrderBook::OrderBook(SymbolId symbol_id) : symbol_id_(symbol_id) {}

bool OrderBook::add_order(Order order) {
  if (!is_valid_resting_order(order) || order.symbol_id != symbol_id_) {
    return false;
  }
  if (order_index_.find(order.order_id) != order_index_.end()) {
    return false;
  }

  PriceLevel* price_level = mutable_level(order.side, order.price_ticks);
  if (price_level == nullptr) {
    return false;
  }

  price_level->price_ticks = order.price_ticks;
  price_level->total_quantity += order.quantity;
  price_level->orders.push_back(order);
  auto iterator = std::prev(price_level->orders.end());
  order_index_.emplace(order.order_id, Locator{order.side, order.price_ticks, iterator});
  return true;
}

bool OrderBook::cancel_order(OrderId order_id) {
  const auto locator_it = order_index_.find(order_id);
  if (locator_it == order_index_.end()) {
    return false;
  }

  const Locator locator = locator_it->second;
  PriceLevel* price_level = mutable_level(locator.side, locator.price_ticks);
  if (price_level == nullptr) {
    return false;
  }

  price_level->total_quantity -= locator.iterator->quantity;
  price_level->orders.erase(locator.iterator);
  order_index_.erase(locator_it);
  erase_level_if_empty(locator.side, locator.price_ticks);
  return true;
}

bool OrderBook::reduce_order(OrderId order_id, Quantity quantity) {
  if (quantity <= 0) {
    return false;
  }

  const auto locator_it = order_index_.find(order_id);
  if (locator_it == order_index_.end()) {
    return false;
  }

  Locator& locator = locator_it->second;
  PriceLevel* price_level = mutable_level(locator.side, locator.price_ticks);
  if (price_level == nullptr) {
    return false;
  }

  Order& order = *locator.iterator;
  if (quantity >= order.quantity) {
    return cancel_order(order_id);
  }

  order.quantity -= quantity;
  price_level->total_quantity -= quantity;
  return true;
}

bool OrderBook::replace_order(OrderId order_id, PriceTicks new_price_ticks, Quantity new_quantity,
                              TimestampNs timestamp_ns, SequenceNumber sequence_number) {
  if (new_price_ticks <= 0 || new_quantity <= 0) {
    return false;
  }

  const Order* existing = find_order(order_id);
  if (existing == nullptr) {
    return false;
  }

  Order replacement = *existing;
  replacement.price_ticks = new_price_ticks;
  replacement.quantity = new_quantity;
  replacement.timestamp_ns = timestamp_ns;
  replacement.sequence_number = sequence_number;

  if (!cancel_order(order_id)) {
    return false;
  }
  return add_order(replacement);
}

const Order* OrderBook::find_order(OrderId order_id) const {
  const auto locator_it = order_index_.find(order_id);
  if (locator_it == order_index_.end()) {
    return nullptr;
  }
  return &(*locator_it->second.iterator);
}

Order* OrderBook::mutable_best_order(Side resting_side) {
  if (resting_side == Side::Buy) {
    if (bids_.empty()) {
      return nullptr;
    }
    return &bids_.begin()->second.orders.front();
  }
  if (resting_side == Side::Sell) {
    if (asks_.empty()) {
      return nullptr;
    }
    return &asks_.begin()->second.orders.front();
  }
  return nullptr;
}

const Order* OrderBook::best_order(Side resting_side) const {
  if (resting_side == Side::Buy) {
    if (bids_.empty()) {
      return nullptr;
    }
    return &bids_.begin()->second.orders.front();
  }
  if (resting_side == Side::Sell) {
    if (asks_.empty()) {
      return nullptr;
    }
    return &asks_.begin()->second.orders.front();
  }
  return nullptr;
}

std::optional<PriceTicks> OrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  return bids_.begin()->first;
}

std::optional<PriceTicks> OrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  return asks_.begin()->first;
}

Quantity OrderBook::total_quantity_at(Side side, PriceTicks price_ticks) const {
  const PriceLevel* price_level = level(side, price_ticks);
  if (price_level == nullptr) {
    return 0;
  }
  return price_level->total_quantity;
}

L2View OrderBook::l2_view(std::size_t depth) const {
  L2View view;
  view.symbol_id = symbol_id_;

  for (const auto& [price, price_level] : bids_) {
    if (view.bids.size() >= depth) {
      break;
    }
    view.bids.push_back(L2Level{price, price_level.total_quantity});
  }

  for (const auto& [price, price_level] : asks_) {
    if (view.asks.size() >= depth) {
      break;
    }
    view.asks.push_back(L2Level{price, price_level.total_quantity});
  }

  return view;
}

BookInvariantReport OrderBook::check_invariants() const {
  BookInvariantReport report;
  std::unordered_set<OrderId> seen_orders;

  const auto check_levels = [&](Side side, const auto& levels) {
    for (const auto& [price, price_level] : levels) {
      if (price_level.orders.empty()) {
        add_violation(report, "empty price level was retained");
      }
      if (price_level.price_ticks != price) {
        add_violation(report, "price level key and stored price differ");
      }

      Quantity child_sum = 0;
      for (const auto& order : price_level.orders) {
        if (order.quantity <= 0) {
          add_violation(report, "resting order has non-positive quantity");
        }
        if (!seen_orders.insert(order.order_id).second) {
          add_violation(report, "duplicate order id in price queues");
        }
        if (order.side != side || order.price_ticks != price) {
          add_violation(report, "order side or price does not match containing level");
        }

        const auto locator_it = order_index_.find(order.order_id);
        if (locator_it == order_index_.end()) {
          add_violation(report, "order id missing from lookup index");
        } else if (locator_it->second.side != side || locator_it->second.price_ticks != price ||
                   locator_it->second.iterator->order_id != order.order_id) {
          add_violation(report, "lookup index locator points to inconsistent order");
        }

        child_sum += order.quantity;
      }

      if (child_sum != price_level.total_quantity) {
        add_violation(report, "price-level total does not match child order sum");
      }
    }
  };

  check_levels(Side::Buy, bids_);
  check_levels(Side::Sell, asks_);

  if (seen_orders.size() != order_index_.size()) {
    add_violation(report, "lookup index size differs from queued order count");
  }
  for (const auto& [order_id, locator] : order_index_) {
    if (seen_orders.find(order_id) == seen_orders.end()) {
      add_violation(report, "lookup index contains an order not present in queues");
    }
    if (locator.side == Side::Buy && bids_.find(locator.price_ticks) == bids_.end()) {
      add_violation(report, "bid locator references missing price level");
    }
    if (locator.side == Side::Sell && asks_.find(locator.price_ticks) == asks_.end()) {
      add_violation(report, "ask locator references missing price level");
    }
  }

  if (!bids_.empty() && bids_.begin()->second.orders.empty()) {
    add_violation(report, "best bid level is empty");
  }
  if (!asks_.empty() && asks_.begin()->second.orders.empty()) {
    add_violation(report, "best ask level is empty");
  }

  return report;
}

std::uint64_t OrderBook::checksum() const {
  std::uint64_t seed = kFnvOffsetBasis;
  seed = checksum_append(seed, symbol_id_);
  seed = checksum_levels(seed, Side::Buy, bids_);
  seed = checksum_levels(seed, Side::Sell, asks_);
  return seed;
}

PriceLevel* OrderBook::mutable_level(Side side, PriceTicks price_ticks) {
  if (side == Side::Buy) {
    auto [it, _] = bids_.try_emplace(price_ticks);
    return &it->second;
  }
  if (side == Side::Sell) {
    auto [it, _] = asks_.try_emplace(price_ticks);
    return &it->second;
  }
  return nullptr;
}

const PriceLevel* OrderBook::level(Side side, PriceTicks price_ticks) const {
  if (side == Side::Buy) {
    const auto it = bids_.find(price_ticks);
    return it == bids_.end() ? nullptr : &it->second;
  }
  if (side == Side::Sell) {
    const auto it = asks_.find(price_ticks);
    return it == asks_.end() ? nullptr : &it->second;
  }
  return nullptr;
}

void OrderBook::erase_level_if_empty(Side side, PriceTicks price_ticks) {
  if (side == Side::Buy) {
    const auto it = bids_.find(price_ticks);
    if (it != bids_.end() && it->second.orders.empty()) {
      bids_.erase(it);
    }
    return;
  }

  if (side == Side::Sell) {
    const auto it = asks_.find(price_ticks);
    if (it != asks_.end() && it->second.orders.empty()) {
      asks_.erase(it);
    }
  }
}

} // namespace asterion
