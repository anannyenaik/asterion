#include "asterion/matching/matching_engine.hpp"

#include "asterion/core/checksum.hpp"

#include <algorithm>

namespace asterion {

MatchingEngine::MatchingEngine(SymbolId symbol_id)
    : symbol_id_(symbol_id), book_(symbol_id), reports_checksum_(kFnvOffsetBasis) {}

std::vector<ExecutionReport> MatchingEngine::submit_order(const NewOrderRequest& request) {
  std::vector<ExecutionReport> reports;

  if (request.symbol_id != symbol_id_ || !is_valid_side(request.side)) {
    record_report(reports, make_reject(request.client_order_id, request.symbol_id, request.side,
                                       request.timestamp_ns, RejectReason::Unsupported));
    return reports;
  }
  if (request.quantity <= 0) {
    record_report(reports, make_reject(request.client_order_id, request.symbol_id, request.side,
                                       request.timestamp_ns, RejectReason::InvalidQuantity));
    return reports;
  }
  if (request.order_type == OrderType::Limit && request.price_ticks <= 0) {
    record_report(reports, make_reject(request.client_order_id, request.symbol_id, request.side,
                                       request.timestamp_ns, RejectReason::InvalidPrice));
    return reports;
  }
  if (!reserve_client_order_id(request.client_order_id)) {
    record_report(reports,
                  make_reject(request.client_order_id, request.symbol_id, request.side,
                              request.timestamp_ns, RejectReason::DuplicateClientOrderId));
    return reports;
  }

  const OrderId exchange_order_id = next_exchange_order_id_++;
  OrderState state;
  state.client_order_id = request.client_order_id;
  state.exchange_order_id = exchange_order_id;
  state.symbol_id = request.symbol_id;
  state.side = request.side;
  state.order_type = request.order_type;
  state.limit_price_ticks = request.price_ticks;
  state.original_quantity = request.quantity;
  state.timestamp_ns = request.timestamp_ns;
  states_.emplace(exchange_order_id, state);
  client_to_exchange_.emplace(request.client_order_id, exchange_order_id);

  OrderState& stored_state = states_.at(exchange_order_id);
  record_report(reports, make_report(stored_state, ExecType::New, 0, 0, request.timestamp_ns,
                                     RejectReason::None));

  Quantity remaining = request.quantity;
  match_against_book(stored_state, remaining, request.timestamp_ns, reports);

  if (remaining > 0 && request.order_type == OrderType::Limit) {
    if (stored_state.filled_quantity > 0) {
      stored_state.status = OrderStatus::PartiallyFilled;
    }
    rest_limit_order(stored_state, remaining, request.timestamp_ns);
  } else if (remaining > 0 && request.order_type == OrderType::Market) {
    stored_state.status = OrderStatus::Canceled;
    record_report(reports, make_report(stored_state, ExecType::Canceled, 0, 0,
                                       request.timestamp_ns, RejectReason::None));
  }

  return reports;
}

std::vector<ExecutionReport> MatchingEngine::cancel_order(const CancelOrderRequest& request) {
  std::vector<ExecutionReport> reports;
  if (!reserve_client_order_id(request.client_order_id)) {
    record_report(reports, make_reject(request.client_order_id, symbol_id_, Side::None,
                                       request.timestamp_ns, RejectReason::DuplicateClientOrderId));
    return reports;
  }

  auto state_it = states_.find(request.exchange_order_id);
  if (state_it == states_.end() || book_.find_order(request.exchange_order_id) == nullptr) {
    record_report(reports, make_reject(request.client_order_id, symbol_id_, Side::None,
                                       request.timestamp_ns, RejectReason::UnknownOrder));
    return reports;
  }

  OrderState& state = state_it->second;
  book_.cancel_order(request.exchange_order_id);
  state.status = OrderStatus::Canceled;
  record_report(reports,
                make_report(state, ExecType::Canceled, 0, 0, request.timestamp_ns,
                            RejectReason::None));
  return reports;
}

std::vector<ExecutionReport> MatchingEngine::replace_order(const ReplaceOrderRequest& request) {
  std::vector<ExecutionReport> reports;
  if (request.new_quantity <= 0) {
    record_report(reports, make_reject(request.client_order_id, symbol_id_, Side::None,
                                       request.timestamp_ns, RejectReason::InvalidQuantity));
    return reports;
  }
  if (request.new_price_ticks <= 0) {
    record_report(reports, make_reject(request.client_order_id, symbol_id_, Side::None,
                                       request.timestamp_ns, RejectReason::InvalidPrice));
    return reports;
  }
  if (!reserve_client_order_id(request.client_order_id)) {
    record_report(reports, make_reject(request.client_order_id, symbol_id_, Side::None,
                                       request.timestamp_ns, RejectReason::DuplicateClientOrderId));
    return reports;
  }

  auto state_it = states_.find(request.exchange_order_id);
  const Order* resting = book_.find_order(request.exchange_order_id);
  if (state_it == states_.end() || resting == nullptr) {
    record_report(reports, make_reject(request.client_order_id, symbol_id_, Side::None,
                                       request.timestamp_ns, RejectReason::UnknownOrder));
    return reports;
  }

  OrderState& state = state_it->second;
  book_.cancel_order(request.exchange_order_id);
  state.limit_price_ticks = request.new_price_ticks;
  state.original_quantity = state.filled_quantity + request.new_quantity;
  state.status = OrderStatus::Replaced;
  state.timestamp_ns = request.timestamp_ns;
  record_report(reports,
                make_report(state, ExecType::Replaced, 0, 0, request.timestamp_ns,
                            RejectReason::None));

  Quantity remaining = request.new_quantity;
  match_against_book(state, remaining, request.timestamp_ns, reports);
  if (remaining > 0) {
    if (state.filled_quantity > 0 && state.status != OrderStatus::Replaced) {
      state.status = OrderStatus::PartiallyFilled;
    }
    rest_limit_order(state, remaining, request.timestamp_ns);
  }

  return reports;
}

bool MatchingEngine::crosses(Side incoming_side, OrderType order_type,
                             PriceTicks limit_price_ticks) const {
  if (incoming_side == Side::Buy) {
    const auto best = book_.best_ask();
    if (!best.has_value()) {
      return false;
    }
    return order_type == OrderType::Market || limit_price_ticks >= *best;
  }

  if (incoming_side == Side::Sell) {
    const auto best = book_.best_bid();
    if (!best.has_value()) {
      return false;
    }
    return order_type == OrderType::Market || limit_price_ticks <= *best;
  }

  return false;
}

void MatchingEngine::match_against_book(OrderState& incoming, Quantity& remaining,
                                        TimestampNs timestamp_ns,
                                        std::vector<ExecutionReport>& reports) {
  while (remaining > 0 && crosses(incoming.side, incoming.order_type, incoming.limit_price_ticks)) {
    Order* resting_order = book_.mutable_best_order(opposite(incoming.side));
    if (resting_order == nullptr) {
      return;
    }

    const OrderId resting_order_id = resting_order->order_id;
    const PriceTicks fill_price = resting_order->price_ticks;
    const Quantity fill_quantity = std::min(remaining, resting_order->quantity);
    auto resting_state_it = states_.find(resting_order_id);
    if (resting_state_it == states_.end()) {
      incoming.status = OrderStatus::Rejected;
      record_report(reports, make_report(incoming, ExecType::Rejected, 0, 0, timestamp_ns,
                                         RejectReason::InternalError));
      return;
    }

    OrderState& resting_state = resting_state_it->second;
    remaining -= fill_quantity;

    resting_state.filled_quantity += fill_quantity;
    resting_state.filled_notional_ticks += fill_quantity * fill_price;
    resting_state.status = remaining_quantity(resting_state) == 0 ? OrderStatus::Filled
                                                                  : OrderStatus::PartiallyFilled;

    incoming.filled_quantity += fill_quantity;
    incoming.filled_notional_ticks += fill_quantity * fill_price;
    incoming.status = remaining_quantity(incoming) == 0 ? OrderStatus::Filled
                                                        : OrderStatus::PartiallyFilled;

    record_report(reports, make_report(resting_state, ExecType::Trade, fill_quantity, fill_price,
                                       timestamp_ns, RejectReason::None));
    record_report(reports, make_report(incoming, ExecType::Trade, fill_quantity, fill_price,
                                       timestamp_ns, RejectReason::None));

    book_.reduce_order(resting_order_id, fill_quantity);
  }
}

void MatchingEngine::rest_limit_order(const OrderState& state, Quantity remaining,
                                      TimestampNs timestamp_ns) {
  Order order;
  order.order_id = state.exchange_order_id;
  order.client_order_id = state.client_order_id;
  order.symbol_id = state.symbol_id;
  order.side = state.side;
  order.price_ticks = state.limit_price_ticks;
  order.quantity = remaining;
  order.timestamp_ns = timestamp_ns;
  order.sequence_number = state.exchange_order_id;
  book_.add_order(order);
}

ExecutionReport MatchingEngine::make_report(const OrderState& state, ExecType exec_type,
                                            Quantity last_fill_quantity,
                                            PriceTicks last_fill_price_ticks,
                                            TimestampNs timestamp_ns,
                                            RejectReason reject_reason) const {
  Quantity remaining = remaining_quantity(state);
  if (state.status == OrderStatus::Canceled || state.status == OrderStatus::Filled ||
      state.status == OrderStatus::Rejected) {
    remaining = 0;
  }

  return ExecutionReport{state.client_order_id,
                         state.exchange_order_id,
                         state.symbol_id,
                         state.side,
                         state.status,
                         exec_type,
                         state.filled_quantity,
                         remaining,
                         last_fill_quantity,
                         last_fill_price_ticks,
                         average_price_ticks(state),
                         timestamp_ns,
                         reject_reason};
}

ExecutionReport MatchingEngine::make_reject(ClientOrderId client_order_id, SymbolId symbol_id,
                                            Side side, TimestampNs timestamp_ns,
                                            RejectReason reason) const {
  return ExecutionReport{client_order_id,
                         kInvalidOrderId,
                         symbol_id,
                         side,
                         OrderStatus::Rejected,
                         ExecType::Rejected,
                         0,
                         0,
                         0,
                         0,
                         0,
                         timestamp_ns,
                         reason};
}

void MatchingEngine::record_report(std::vector<ExecutionReport>& reports, ExecutionReport report) {
  reports_checksum_ = append_to_checksum(reports_checksum_, report);
  reports.push_back(report);
}

bool MatchingEngine::reserve_client_order_id(ClientOrderId client_order_id) {
  if (client_order_id == kInvalidClientOrderId) {
    return false;
  }
  return seen_client_order_ids_.insert(client_order_id).second;
}

} // namespace asterion
