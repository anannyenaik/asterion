#pragma once

#include "asterion/core/types.hpp"

namespace asterion {

struct Order {
  OrderId order_id{kInvalidOrderId};
  ClientOrderId client_order_id{kInvalidClientOrderId};
  SymbolId symbol_id{kInvalidSymbolId};
  Side side{Side::None};
  PriceTicks price_ticks{0};
  Quantity quantity{0};
  TimestampNs timestamp_ns{0};
  SequenceNumber sequence_number{0};
};

[[nodiscard]] inline bool is_valid_resting_order(const Order& order) noexcept {
  return order.order_id != kInvalidOrderId && order.symbol_id != kInvalidSymbolId &&
         is_valid_side(order.side) && order.price_ticks > 0 && order.quantity > 0;
}

} // namespace asterion
