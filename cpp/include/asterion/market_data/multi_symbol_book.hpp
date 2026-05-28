#pragma once

#include "asterion/book/order_book.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <span>

namespace asterion {

// Structural groundwork for shared multi-symbol handling.
//
// A MultiSymbolBookSet owns one OrderBook per symbol and applies an interleaved
// market-data stream in a single pass, routing each event to its symbol's book.
// It is deliberately NOT a cross-symbol matching engine and does NOT replace the
// stable grouped replay path (replay_by_symbol): sequence/timestamp validation and
// diagnostics remain the ReplayEngine's responsibility. This class only provides a
// single-pass routing surface and a deterministic combined checksum to build on.
class MultiSymbolBookSet {
public:
  [[nodiscard]] std::size_t symbol_count() const noexcept { return books_.size(); }
  [[nodiscard]] bool contains(SymbolId symbol_id) const {
    return books_.find(symbol_id) != books_.end();
  }

  // Returns the book for symbol_id, creating an empty one on first use.
  OrderBook& book_for(SymbolId symbol_id) {
    return books_.try_emplace(symbol_id, symbol_id).first->second;
  }

  [[nodiscard]] const OrderBook* find_book(SymbolId symbol_id) const {
    const auto it = books_.find(symbol_id);
    return it == books_.end() ? nullptr : &it->second;
  }

  // Apply one event to its symbol's book. Returns false if the event cannot be
  // applied (unknown cancel, duplicate add, invalid order); the set stays consistent.
  [[nodiscard]] bool apply(const MarketDataEvent& event) {
    if (event.symbol_id == kInvalidSymbolId) {
      return false;
    }
    OrderBook& book = book_for(event.symbol_id);
    switch (event.event_type) {
    case MarketEventType::Add:
      return book.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                  event.side, event.price_ticks, event.quantity,
                                  event.timestamp_ns, event.sequence_number});
    case MarketEventType::Cancel:
      if (event.quantity > 0) {
        return book.reduce_order(event.order_id, event.quantity);
      }
      return book.cancel_order(event.order_id);
    case MarketEventType::Replace:
      return book.replace_order(event.order_id, event.price_ticks, event.quantity,
                                event.timestamp_ns, event.sequence_number);
    case MarketEventType::Execute:
      return book.reduce_order(event.order_id, event.quantity);
    case MarketEventType::Snapshot:
      if ((event.flags & kSnapshotBeginFlag) != 0U) {
        book.clear();
      }
      if (event.order_id != kInvalidOrderId) {
        return book.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                    event.side, event.price_ticks, event.quantity,
                                    event.timestamp_ns, event.sequence_number});
      }
      return true;
    case MarketEventType::Trade:
    case MarketEventType::Heartbeat:
      return true;
    }
    return false;
  }

  [[nodiscard]] std::size_t apply_all(std::span<const MarketDataEvent> events) {
    std::size_t applied = 0;
    for (const MarketDataEvent& event : events) {
      if (apply(event)) {
        ++applied;
      }
    }
    return applied;
  }

  // Deterministic checksum over per-symbol book checksums, ordered by symbol id.
  [[nodiscard]] std::uint64_t combined_checksum() const {
    std::uint64_t seed = kFnvOffsetBasis;
    seed = checksum_append(seed, static_cast<std::uint64_t>(books_.size()));
    for (const auto& [symbol_id, book] : books_) {
      seed = checksum_append(seed, symbol_id);
      seed = checksum_append(seed, book.checksum());
    }
    return seed;
  }

private:
  std::map<SymbolId, OrderBook> books_;
};

} // namespace asterion
