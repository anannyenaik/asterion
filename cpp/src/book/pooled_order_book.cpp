#include "asterion/book/pooled_order_book.hpp"

#include "asterion/core/checksum.hpp"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <utility>

namespace asterion {

namespace {

void add_violation(BookInvariantReport& report, std::string message) {
  report.ok = false;
  report.violations.push_back(std::move(message));
}

} // namespace

PooledOrderBook::PooledOrderBook(SymbolId symbol_id) : symbol_id_(symbol_id) {}

void PooledOrderBook::clear() noexcept {
  bids_.clear();
  asks_.clear();
  order_nodes_.clear();
  free_head_ = kNoIndex;
  order_index_.clear();
}

bool PooledOrderBook::add_order(Order order) {
  if (!is_valid_resting_order(order) || order.symbol_id != symbol_id_) {
    return false;
  }
  if (order_index_.find(order.order_id).has_value()) {
    return false;
  }

  LevelNode* price_level = mutable_level(order.side, order.price_ticks);
  if (price_level == nullptr) {
    return false;
  }

  const std::size_t node_index = acquire_node(order);
  append_order(*price_level, order_nodes_, node_index);
  if (!order_index_.insert(order.order_id, node_index)) {
    unlink_order(*price_level, order_nodes_, node_index);
    release_node(node_index);
    erase_level_if_empty(order.side, order.price_ticks);
    return false;
  }
  return true;
}

bool PooledOrderBook::cancel_order(OrderId order_id) {
  const std::optional<std::size_t> node_index = order_index_.find(order_id);
  if (!node_index.has_value()) {
    return false;
  }

  OrderNode& node = order_nodes_[*node_index];
  LevelNode* price_level = mutable_level(node.order.side, node.order.price_ticks);
  if (price_level == nullptr) {
    return false;
  }

  price_level->total_quantity -= node.order.quantity;
  unlink_order(*price_level, order_nodes_, *node_index);
  (void)order_index_.erase(order_id);
  const Side side = node.order.side;
  const PriceTicks price_ticks = node.order.price_ticks;
  release_node(*node_index);
  erase_level_if_empty(side, price_ticks);
  return true;
}

bool PooledOrderBook::reduce_order(OrderId order_id, Quantity quantity) {
  if (quantity <= 0) {
    return false;
  }

  const std::optional<std::size_t> node_index = order_index_.find(order_id);
  if (!node_index.has_value()) {
    return false;
  }

  OrderNode& node = order_nodes_[*node_index];
  if (quantity >= node.order.quantity) {
    return cancel_order(order_id);
  }

  LevelNode* price_level = mutable_level(node.order.side, node.order.price_ticks);
  if (price_level == nullptr) {
    return false;
  }

  node.order.quantity -= quantity;
  price_level->total_quantity -= quantity;
  return true;
}

bool PooledOrderBook::replace_order(OrderId order_id, PriceTicks new_price_ticks,
                                    Quantity new_quantity, TimestampNs timestamp_ns,
                                    SequenceNumber sequence_number) {
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

const Order* PooledOrderBook::find_order(OrderId order_id) const {
  const std::optional<std::size_t> node_index = order_index_.find(order_id);
  if (!node_index.has_value()) {
    return nullptr;
  }
  const OrderNode& node = order_nodes_[*node_index];
  return node.active ? &node.order : nullptr;
}

Order* PooledOrderBook::mutable_best_order(Side resting_side) {
  if (resting_side == Side::Buy) {
    if (bids_.empty()) {
      return nullptr;
    }
    return &order_nodes_[bids_.front().head].order;
  }
  if (resting_side == Side::Sell) {
    if (asks_.empty()) {
      return nullptr;
    }
    return &order_nodes_[asks_.front().head].order;
  }
  return nullptr;
}

const Order* PooledOrderBook::best_order(Side resting_side) const {
  if (resting_side == Side::Buy) {
    if (bids_.empty()) {
      return nullptr;
    }
    return &order_nodes_[bids_.front().head].order;
  }
  if (resting_side == Side::Sell) {
    if (asks_.empty()) {
      return nullptr;
    }
    return &order_nodes_[asks_.front().head].order;
  }
  return nullptr;
}

std::optional<PriceTicks> PooledOrderBook::best_bid() const {
  if (bids_.empty()) {
    return std::nullopt;
  }
  return bids_.front().price_ticks;
}

std::optional<PriceTicks> PooledOrderBook::best_ask() const {
  if (asks_.empty()) {
    return std::nullopt;
  }
  return asks_.front().price_ticks;
}

Quantity PooledOrderBook::total_quantity_at(Side side, PriceTicks price_ticks) const {
  const LevelNode* price_level = level(side, price_ticks);
  if (price_level == nullptr) {
    return 0;
  }
  return price_level->total_quantity;
}

L2View PooledOrderBook::l2_view(std::size_t depth) const {
  L2View view;
  fill_l2_view(depth, view);
  return view;
}

void PooledOrderBook::fill_l2_view(std::size_t depth, L2View& view) const {
  view.symbol_id = symbol_id_;
  view.bids.clear();
  view.asks.clear();

  if (depth == 0) {
    return;
  }

  for (const LevelNode& price_level : bids_) {
    if (view.bids.size() >= depth) {
      break;
    }
    view.bids.push_back(L2Level{price_level.price_ticks, price_level.total_quantity});
  }

  for (const LevelNode& price_level : asks_) {
    if (view.asks.size() >= depth) {
      break;
    }
    view.asks.push_back(L2Level{price_level.price_ticks, price_level.total_quantity});
  }
}

void PooledOrderBook::reserve_order_capacity(std::size_t order_count) {
  order_nodes_.reserve(order_count);
  bids_.reserve(order_count);
  asks_.reserve(order_count);
  order_index_.reserve(order_count);
}

BookInvariantReport PooledOrderBook::check_invariants() const {
  BookInvariantReport report;
  std::unordered_set<OrderId> seen_orders;

  const auto check_levels = [&](Side side, const Levels& levels) {
    PriceTicks previous_price = 0;
    bool has_previous = false;
    for (const LevelNode& price_level : levels) {
      if (price_level.order_count == 0 || price_level.head == kNoIndex ||
          price_level.tail == kNoIndex) {
        add_violation(report, "empty price level was retained");
      }
      if (has_previous) {
        if (side == Side::Buy && previous_price <= price_level.price_ticks) {
          add_violation(report, "bid levels are not sorted best-first");
        }
        if (side == Side::Sell && previous_price >= price_level.price_ticks) {
          add_violation(report, "ask levels are not sorted best-first");
        }
      }
      previous_price = price_level.price_ticks;
      has_previous = true;

      Quantity child_sum = 0;
      std::size_t child_count = 0;
      std::size_t previous_node = kNoIndex;
      std::size_t node_index = price_level.head;
      while (node_index != kNoIndex) {
        if (node_index >= order_nodes_.size()) {
          add_violation(report, "price level references out-of-range order node");
          break;
        }
        const OrderNode& node = order_nodes_[node_index];
        const Order& order = node.order;
        if (!node.active) {
          add_violation(report, "price level references inactive order node");
        }
        if (node.previous != previous_node) {
          add_violation(report, "order queue previous link is inconsistent");
        }
        if (order.quantity <= 0) {
          add_violation(report, "resting order has non-positive quantity");
        }
        if (!seen_orders.insert(order.order_id).second) {
          add_violation(report, "duplicate order id in price queues");
        }
        if (order.side != side || order.price_ticks != price_level.price_ticks) {
          add_violation(report, "order side or price does not match containing level");
        }

        const std::optional<std::size_t> indexed_node = order_index_.find(order.order_id);
        if (!indexed_node.has_value()) {
          add_violation(report, "order id missing from lookup index");
        } else if (*indexed_node != node_index) {
          add_violation(report, "lookup index locator points to inconsistent order");
        }

        child_sum += order.quantity;
        ++child_count;
        previous_node = node_index;
        node_index = node.next;
      }

      if (previous_node != price_level.tail) {
        add_violation(report, "order queue tail link is inconsistent");
      }
      if (child_count != price_level.order_count) {
        add_violation(report, "price-level order count does not match linked orders");
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
  for (std::size_t node_index = 0; node_index < order_nodes_.size(); ++node_index) {
    const OrderNode& node = order_nodes_[node_index];
    if (!node.active) {
      continue;
    }
    if (seen_orders.find(node.order.order_id) == seen_orders.end()) {
      add_violation(report, "active order node is not present in queues");
    }
    const std::optional<std::size_t> indexed_node = order_index_.find(node.order.order_id);
    if (!indexed_node.has_value() || *indexed_node != node_index) {
      add_violation(report, "active order node is not present in lookup index");
    }
  }

  return report;
}

std::uint64_t PooledOrderBook::checksum() const {
  std::uint64_t seed = kFnvOffsetBasis;
  seed = checksum_append(seed, symbol_id_);
  seed = checksum_levels(seed, Side::Buy, bids_);
  seed = checksum_levels(seed, Side::Sell, asks_);
  return seed;
}

PooledOrderBook::LevelNode* PooledOrderBook::mutable_level(Side side,
                                                           PriceTicks price_ticks) {
  if (side == Side::Buy) {
    return mutable_bid_level(bids_, price_ticks);
  }
  if (side == Side::Sell) {
    return mutable_ask_level(asks_, price_ticks);
  }
  return nullptr;
}

const PooledOrderBook::LevelNode* PooledOrderBook::level(Side side,
                                                         PriceTicks price_ticks) const {
  if (side == Side::Buy) {
    return find_level(bids_, price_ticks);
  }
  if (side == Side::Sell) {
    return find_level(asks_, price_ticks);
  }
  return nullptr;
}

void PooledOrderBook::erase_level_if_empty(Side side, PriceTicks price_ticks) {
  Levels* levels = nullptr;
  if (side == Side::Buy) {
    levels = &bids_;
  } else if (side == Side::Sell) {
    levels = &asks_;
  }
  if (levels == nullptr) {
    return;
  }
  const auto it = std::find_if(levels->begin(), levels->end(), [&](const LevelNode& level_node) {
    return level_node.price_ticks == price_ticks;
  });
  if (it != levels->end() && it->order_count == 0) {
    levels->erase(it);
  }
}

std::size_t PooledOrderBook::acquire_node(Order order) {
  if (free_head_ != kNoIndex) {
    const std::size_t node_index = free_head_;
    OrderNode& node = order_nodes_[node_index];
    free_head_ = node.next;
    node = OrderNode{order, kNoIndex, kNoIndex, true};
    return node_index;
  }

  order_nodes_.push_back(OrderNode{order, kNoIndex, kNoIndex, true});
  return order_nodes_.size() - 1U;
}

void PooledOrderBook::release_node(std::size_t node_index) noexcept {
  OrderNode& node = order_nodes_[node_index];
  node.active = false;
  node.previous = kNoIndex;
  node.next = free_head_;
  free_head_ = node_index;
}

void PooledOrderBook::append_order(LevelNode& level, std::vector<OrderNode>& nodes,
                                   std::size_t node_index) noexcept {
  OrderNode& node = nodes[node_index];
  node.previous = level.tail;
  node.next = kNoIndex;
  if (level.tail != kNoIndex) {
    nodes[level.tail].next = node_index;
  } else {
    level.head = node_index;
  }
  level.tail = node_index;
  level.total_quantity += node.order.quantity;
  ++level.order_count;
}

void PooledOrderBook::unlink_order(LevelNode& level, std::vector<OrderNode>& nodes,
                                   std::size_t node_index) noexcept {
  const OrderNode& node = nodes[node_index];
  if (node.previous != kNoIndex) {
    nodes[node.previous].next = node.next;
  } else {
    level.head = node.next;
  }
  if (node.next != kNoIndex) {
    nodes[node.next].previous = node.previous;
  } else {
    level.tail = node.previous;
  }
  --level.order_count;
}

PooledOrderBook::LevelNode* PooledOrderBook::mutable_bid_level(Levels& levels,
                                                               PriceTicks price_ticks) {
  const auto it = std::find_if(levels.begin(), levels.end(), [&](const LevelNode& level_node) {
    return level_node.price_ticks <= price_ticks;
  });
  if (it != levels.end() && it->price_ticks == price_ticks) {
    return &(*it);
  }
  return &(*levels.insert(it, LevelNode{price_ticks, 0, kNoIndex, kNoIndex, 0}));
}

PooledOrderBook::LevelNode* PooledOrderBook::mutable_ask_level(Levels& levels,
                                                               PriceTicks price_ticks) {
  const auto it = std::find_if(levels.begin(), levels.end(), [&](const LevelNode& level_node) {
    return level_node.price_ticks >= price_ticks;
  });
  if (it != levels.end() && it->price_ticks == price_ticks) {
    return &(*it);
  }
  return &(*levels.insert(it, LevelNode{price_ticks, 0, kNoIndex, kNoIndex, 0}));
}

const PooledOrderBook::LevelNode* PooledOrderBook::find_level(
    const Levels& levels, PriceTicks price_ticks) noexcept {
  const auto it = std::find_if(levels.begin(), levels.end(), [&](const LevelNode& level_node) {
    return level_node.price_ticks == price_ticks;
  });
  return it == levels.end() ? nullptr : &(*it);
}

std::uint64_t PooledOrderBook::checksum_levels(std::uint64_t seed, Side side,
                                               const Levels& levels) const {
  seed = checksum_append(seed, side);
  seed = checksum_append(seed, static_cast<std::uint64_t>(levels.size()));
  for (const LevelNode& level : levels) {
    seed = checksum_append(seed, level.price_ticks);
    seed = checksum_append(seed, level.total_quantity);
    seed = checksum_append(seed, static_cast<std::uint64_t>(level.order_count));
    std::size_t node_index = level.head;
    while (node_index != kNoIndex) {
      const Order& order = order_nodes_[node_index].order;
      seed = checksum_append(seed, order.order_id);
      seed = checksum_append(seed, order.client_order_id);
      seed = checksum_append(seed, order.symbol_id);
      seed = checksum_append(seed, order.quantity);
      seed = checksum_append(seed, order.timestamp_ns);
      seed = checksum_append(seed, order.sequence_number);
      node_index = order_nodes_[node_index].next;
    }
  }
  return seed;
}

void PooledOrderBook::FlatOrderIndex::clear() noexcept {
  for (Entry& entry : entries_) {
    entry.state = SlotState::Empty;
    entry.order_id = kInvalidOrderId;
    entry.node_index = kNoIndex;
  }
  size_ = 0;
}

void PooledOrderBook::FlatOrderIndex::reserve(std::size_t expected_size) {
  const std::size_t required_capacity = next_capacity(expected_size);
  if (entries_.size() >= required_capacity) {
    return;
  }
  rehash(required_capacity);
}

bool PooledOrderBook::FlatOrderIndex::insert(OrderId order_id, std::size_t node_index) {
  if (order_id == kInvalidOrderId) {
    return false;
  }
  if (entries_.empty() || (size_ + 1U) * 2U > entries_.size()) {
    rehash(next_capacity(size_ + 1U));
  }

  std::size_t first_deleted = kNoIndex;
  std::size_t index = bucket(order_id);
  for (std::size_t probes = 0; probes < entries_.size(); ++probes) {
    Entry& entry = entries_[index];
    if (entry.state == SlotState::Occupied) {
      if (entry.order_id == order_id) {
        return false;
      }
    } else if (entry.state == SlotState::Deleted) {
      if (first_deleted == kNoIndex) {
        first_deleted = index;
      }
    } else {
      const std::size_t target = first_deleted == kNoIndex ? index : first_deleted;
      entries_[target] = Entry{order_id, node_index, SlotState::Occupied};
      ++size_;
      return true;
    }
    index = (index + 1U) & (entries_.size() - 1U);
  }

  if (first_deleted != kNoIndex) {
    entries_[first_deleted] = Entry{order_id, node_index, SlotState::Occupied};
    ++size_;
    return true;
  }

  rehash(entries_.size() * 2U);
  return insert(order_id, node_index);
}

std::optional<std::size_t> PooledOrderBook::FlatOrderIndex::find(
    OrderId order_id) const noexcept {
  if (entries_.empty() || order_id == kInvalidOrderId) {
    return std::nullopt;
  }

  std::size_t index = bucket(order_id);
  for (std::size_t probes = 0; probes < entries_.size(); ++probes) {
    const Entry& entry = entries_[index];
    if (entry.state == SlotState::Empty) {
      return std::nullopt;
    }
    if (entry.state == SlotState::Occupied && entry.order_id == order_id) {
      return entry.node_index;
    }
    index = (index + 1U) & (entries_.size() - 1U);
  }
  return std::nullopt;
}

bool PooledOrderBook::FlatOrderIndex::erase(OrderId order_id) noexcept {
  if (entries_.empty() || order_id == kInvalidOrderId) {
    return false;
  }

  std::size_t index = bucket(order_id);
  for (std::size_t probes = 0; probes < entries_.size(); ++probes) {
    Entry& entry = entries_[index];
    if (entry.state == SlotState::Empty) {
      return false;
    }
    if (entry.state == SlotState::Occupied && entry.order_id == order_id) {
      entry.state = SlotState::Deleted;
      entry.order_id = kInvalidOrderId;
      entry.node_index = kNoIndex;
      --size_;
      return true;
    }
    index = (index + 1U) & (entries_.size() - 1U);
  }
  return false;
}

std::size_t PooledOrderBook::FlatOrderIndex::next_capacity(
    std::size_t expected_size) noexcept {
  std::size_t capacity = 8;
  const std::size_t target = std::max<std::size_t>(8, expected_size * 4U);
  while (capacity < target) {
    capacity <<= 1U;
  }
  return capacity;
}

std::size_t PooledOrderBook::FlatOrderIndex::bucket(OrderId order_id) const noexcept {
  std::uint64_t value = order_id;
  value ^= value >> 33U;
  value *= 0xff51afd7ed558ccdULL;
  value ^= value >> 33U;
  value *= 0xc4ceb9fe1a85ec53ULL;
  value ^= value >> 33U;
  return static_cast<std::size_t>(value) & (entries_.size() - 1U);
}

void PooledOrderBook::FlatOrderIndex::rehash(std::size_t capacity) {
  std::vector<Entry> old_entries = std::move(entries_);
  entries_.assign(capacity, Entry{});
  size_ = 0;
  for (const Entry& entry : old_entries) {
    if (entry.state == SlotState::Occupied) {
      insert_rehashed(entry.order_id, entry.node_index);
    }
  }
}

void PooledOrderBook::FlatOrderIndex::insert_rehashed(OrderId order_id,
                                                      std::size_t node_index) noexcept {
  std::size_t index = bucket(order_id);
  while (entries_[index].state == SlotState::Occupied) {
    index = (index + 1U) & (entries_.size() - 1U);
  }
  entries_[index] = Entry{order_id, node_index, SlotState::Occupied};
  ++size_;
}

} // namespace asterion
