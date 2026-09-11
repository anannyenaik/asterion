#pragma once

#include "asterion/matching/execution_report.hpp"

namespace asterion {

struct OrderState {
  ClientOrderId client_order_id{kInvalidClientOrderId};
  OrderId exchange_order_id{kInvalidOrderId};
  SymbolId symbol_id{kInvalidSymbolId};
  Side side{Side::None};
  OrderType order_type{OrderType::Limit};
  PriceTicks limit_price_ticks{0};
  Quantity original_quantity{0};
  Quantity filled_quantity{0};
  std::int64_t filled_notional_ticks{0};
  ClientId client_id{0};
  OrderStatus status{OrderStatus::New};
  TimestampNs timestamp_ns{0};
};

[[nodiscard]] inline Quantity remaining_quantity(const OrderState& state) noexcept {
  const Quantity remaining = state.original_quantity - state.filled_quantity;
  return remaining > 0 ? remaining : 0;
}

[[nodiscard]] inline PriceTicks average_price_ticks(const OrderState& state) noexcept {
  if (state.filled_quantity <= 0) {
    return 0;
  }
  return static_cast<PriceTicks>(state.filled_notional_ticks / state.filled_quantity);
}

} // namespace asterion
