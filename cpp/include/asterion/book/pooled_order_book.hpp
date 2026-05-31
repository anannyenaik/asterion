#pragma once

#include "asterion/book/l2_view.hpp"
#include "asterion/book/order.hpp"
#include "asterion/book/order_book.hpp"
#include "asterion/core/types.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace asterion {

// Opt-in L3 book used by allocation-sensitive benchmark paths.
//
// The correctness-first OrderBook remains the default. PooledOrderBook keeps its
// mutable storage in vectors plus a reusable flat order-id index so warmed replay
// loops can clear and rebuild the book without paying std::map/std::list node
// allocation costs on each pass.
class PooledOrderBook {
public:
  explicit PooledOrderBook(SymbolId symbol_id);

  [[nodiscard]] SymbolId symbol_id() const noexcept { return symbol_id_; }
  [[nodiscard]] bool empty() const noexcept { return order_index_.empty(); }
  [[nodiscard]] std::size_t order_count() const noexcept { return order_index_.size(); }

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
  static constexpr std::size_t kNoIndex = static_cast<std::size_t>(-1);

  struct LevelNode {
    PriceTicks price_ticks{0};
    Quantity total_quantity{0};
    std::size_t head{kNoIndex};
    std::size_t tail{kNoIndex};
    std::size_t order_count{0};
  };

  struct OrderNode {
    Order order;
    std::size_t previous{kNoIndex};
    std::size_t next{kNoIndex};
    bool active{false};
  };

  class FlatOrderIndex {
  public:
    [[nodiscard]] bool empty() const noexcept { return size_ == 0; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    void clear() noexcept;
    void reserve(std::size_t expected_size);
    [[nodiscard]] bool insert(OrderId order_id, std::size_t node_index);
    [[nodiscard]] std::optional<std::size_t> find(OrderId order_id) const noexcept;
    [[nodiscard]] bool erase(OrderId order_id) noexcept;

  private:
    enum class SlotState : std::uint8_t { Empty, Occupied, Deleted };

    struct Entry {
      OrderId order_id{kInvalidOrderId};
      std::size_t node_index{kNoIndex};
      SlotState state{SlotState::Empty};
    };

    [[nodiscard]] static std::size_t next_capacity(std::size_t expected_size) noexcept;
    [[nodiscard]] std::size_t bucket(OrderId order_id) const noexcept;
    void rehash(std::size_t capacity);
    void insert_rehashed(OrderId order_id, std::size_t node_index) noexcept;

    std::vector<Entry> entries_;
    std::size_t size_{0};
  };

  using Levels = std::vector<LevelNode>;

  [[nodiscard]] LevelNode* mutable_level(Side side, PriceTicks price_ticks);
  [[nodiscard]] const LevelNode* level(Side side, PriceTicks price_ticks) const;
  void erase_level_if_empty(Side side, PriceTicks price_ticks);
  [[nodiscard]] std::size_t acquire_node(Order order);
  void release_node(std::size_t node_index) noexcept;
  static void append_order(LevelNode& level, std::vector<OrderNode>& nodes,
                           std::size_t node_index) noexcept;
  static void unlink_order(LevelNode& level, std::vector<OrderNode>& nodes,
                           std::size_t node_index) noexcept;
  [[nodiscard]] static LevelNode* mutable_bid_level(Levels& levels, PriceTicks price_ticks);
  [[nodiscard]] static LevelNode* mutable_ask_level(Levels& levels, PriceTicks price_ticks);
  [[nodiscard]] static const LevelNode* find_level(const Levels& levels,
                                                   PriceTicks price_ticks) noexcept;
  [[nodiscard]] std::uint64_t checksum_levels(std::uint64_t seed, Side side,
                                              const Levels& levels) const;

  SymbolId symbol_id_;
  Levels bids_;
  Levels asks_;
  std::vector<OrderNode> order_nodes_;
  std::size_t free_head_{kNoIndex};
  FlatOrderIndex order_index_;
};

} // namespace asterion
