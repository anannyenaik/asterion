#pragma once

#include "asterion/book/l2_view.hpp"
#include "asterion/book/price_level.hpp"
#include "asterion/core/types.hpp"

#include <cstddef>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace asterion {

struct BookInvariantReport {
  bool ok{true};
  std::vector<std::string> violations;
};

class OrderBook {
public:
  explicit OrderBook(SymbolId symbol_id);

  [[nodiscard]] SymbolId symbol_id() const noexcept { return symbol_id_; }
  [[nodiscard]] bool empty() const noexcept { return order_index_.empty(); }
  [[nodiscard]] std::size_t order_count() const noexcept { return order_index_.size(); }

  // Remove every resting order and price level, returning the book to its
  // freshly-constructed (empty) state. Used to reset before loading a snapshot.
  void clear() noexcept;

  [[nodiscard]] bool add_order(Order order);
  [[nodiscard]] bool cancel_order(OrderId order_id);
  [[nodiscard]] bool reduce_order(OrderId order_id, Quantity quantity);
  [[nodiscard]] bool replace_order(OrderId order_id, PriceTicks new_price_ticks,
                                   Quantity new_quantity, TimestampNs timestamp_ns,
                                   SequenceNumber sequence_number);

  [[nodiscard]] const Order* find_order(OrderId order_id) const;
  [[nodiscard]] Order* mutable_best_order(Side resting_side);
  [[nodiscard]] const Order* best_order(Side resting_side) const;

  [[nodiscard]] std::optional<PriceTicks> best_bid() const;
  [[nodiscard]] std::optional<PriceTicks> best_ask() const;
  [[nodiscard]] Quantity total_quantity_at(Side side, PriceTicks price_ticks) const;
  [[nodiscard]] L2View l2_view(std::size_t depth) const;
  void fill_l2_view(std::size_t depth, L2View& view) const;
  void reserve_order_capacity(std::size_t order_count);
  [[nodiscard]] BookInvariantReport check_invariants() const;
  [[nodiscard]] std::uint64_t checksum() const;

private:
  using OrderQueue = std::list<Order>;
  using AskLevels = std::map<PriceTicks, PriceLevel>;
  using BidLevels = std::map<PriceTicks, PriceLevel, std::greater<>>;

  struct Locator {
    Side side{Side::None};
    PriceTicks price_ticks{0};
    OrderQueue::iterator iterator;
  };

  [[nodiscard]] PriceLevel* mutable_level(Side side, PriceTicks price_ticks);
  [[nodiscard]] const PriceLevel* level(Side side, PriceTicks price_ticks) const;
  void erase_level_if_empty(Side side, PriceTicks price_ticks);

  SymbolId symbol_id_;
  BidLevels bids_;
  AskLevels asks_;
  std::unordered_map<OrderId, Locator> order_index_;
};

} // namespace asterion
