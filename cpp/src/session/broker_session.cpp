#include "asterion/session/broker_session.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace asterion {

std::string_view to_string(SessionConnectionState state) noexcept {
  switch (state) {
  case SessionConnectionState::Disconnected:
    return "disconnected";
  case SessionConnectionState::Connected:
    return "connected";
  }
  return "unknown";
}

std::string_view to_string(BrokerOrderState state) noexcept {
  switch (state) {
  case BrokerOrderState::Accepted:
    return "accepted";
  case BrokerOrderState::PendingCancel:
    return "pending_cancel";
  case BrokerOrderState::Canceled:
    return "canceled";
  case BrokerOrderState::Filled:
    return "filled";
  }
  return "unknown";
}

std::string_view to_string(BrokerEventType type) noexcept {
  switch (type) {
  case BrokerEventType::Connect:
    return "connect";
  case BrokerEventType::Disconnect:
    return "disconnect";
  case BrokerEventType::Reconnect:
    return "reconnect";
  case BrokerEventType::OrderAccepted:
    return "order_accepted";
  case BrokerEventType::CancelRequested:
    return "cancel_requested";
  case BrokerEventType::CancelAcknowledged:
    return "cancel_acknowledged";
  case BrokerEventType::CancelRejected:
    return "cancel_rejected";
  case BrokerEventType::OrderFilled:
    return "order_filled";
  }
  return "unknown";
}

std::uint64_t append_to_checksum(std::uint64_t seed, const BrokerSessionEvent& event) noexcept {
  seed = checksum_append(seed, event.timestamp_ns);
  seed = checksum_append(seed, event.type);
  seed = checksum_append(seed, event.exchange_order_id);
  seed = checksum_append(seed, event.client_order_id);
  seed = checksum_append(seed, event.symbol_id);
  seed = checksum_append(seed, event.side);
  seed = checksum_append(seed, event.quantity);
  return seed;
}

std::uint64_t checksum_broker_session_events(std::span<const BrokerSessionEvent> events) noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  for (const BrokerSessionEvent& event : events) {
    seed = append_to_checksum(seed, event);
  }
  return seed;
}

void SimulatedBrokerSession::record_event(BrokerSessionEvent event) {
  event_checksum_ = append_to_checksum(event_checksum_, event);
  events_.push_back(std::move(event));
}

void SimulatedBrokerSession::connect(TimestampNs timestamp_ns) {
  if (state_ == SessionConnectionState::Connected) {
    return;
  }
  state_ = SessionConnectionState::Connected;
  ++connect_count_;
  record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::Connect});
}

void SimulatedBrokerSession::disconnect(TimestampNs timestamp_ns) {
  if (state_ == SessionConnectionState::Disconnected) {
    return;
  }
  state_ = SessionConnectionState::Disconnected;
  ++disconnect_count_;
  record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::Disconnect});

  if (cancel_on_disconnect_) {
    // Deterministic order: cancel-request every live order by ascending id.
    std::vector<OrderId> live_ids;
    live_ids.reserve(orders_.size());
    for (const auto& [order_id, record] : orders_) {
      if (record.state == BrokerOrderState::Accepted ||
          record.state == BrokerOrderState::PendingCancel) {
        live_ids.push_back(order_id);
      }
    }
    std::sort(live_ids.begin(), live_ids.end());
    for (const OrderId order_id : live_ids) {
      OrderRecord& record = orders_.at(order_id);
      if (record.state == BrokerOrderState::PendingCancel) {
        continue;
      }
      record.state = BrokerOrderState::PendingCancel;
      record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::CancelRequested, order_id,
                                      record.client_order_id, record.symbol_id, record.side,
                                      record.quantity});
    }
  }

  if (gateway_ != nullptr) {
    gateway_->on_disconnect(timestamp_ns);
  }
}

void SimulatedBrokerSession::reconnect(TimestampNs timestamp_ns) {
  if (state_ == SessionConnectionState::Connected) {
    return;
  }
  state_ = SessionConnectionState::Connected;
  ++reconnect_count_;
  record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::Reconnect});
  if (gateway_ != nullptr) {
    gateway_->on_reconnect(timestamp_ns);
  }
}

bool SimulatedBrokerSession::on_order_accepted(OrderId exchange_order_id,
                                               ClientOrderId client_order_id, SymbolId symbol_id,
                                               Side side, Quantity quantity,
                                               TimestampNs timestamp_ns) {
  if (state_ != SessionConnectionState::Connected || exchange_order_id == kInvalidOrderId) {
    return false;
  }
  if (orders_.find(exchange_order_id) != orders_.end()) {
    return false;
  }
  orders_.emplace(exchange_order_id,
                  OrderRecord{client_order_id, symbol_id, side, quantity,
                              BrokerOrderState::Accepted});
  record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::OrderAccepted, exchange_order_id,
                                  client_order_id, symbol_id, side, quantity});
  return true;
}

bool SimulatedBrokerSession::request_cancel(OrderId exchange_order_id, TimestampNs timestamp_ns) {
  const auto it = orders_.find(exchange_order_id);
  if (it == orders_.end() || it->second.state != BrokerOrderState::Accepted) {
    return false;
  }
  it->second.state = BrokerOrderState::PendingCancel;
  record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::CancelRequested, exchange_order_id,
                                  it->second.client_order_id, it->second.symbol_id,
                                  it->second.side, it->second.quantity});
  return true;
}

bool SimulatedBrokerSession::acknowledge_cancel(OrderId exchange_order_id,
                                                TimestampNs timestamp_ns) {
  const auto it = orders_.find(exchange_order_id);
  if (it == orders_.end() || it->second.state != BrokerOrderState::PendingCancel) {
    return false;
  }
  it->second.state = BrokerOrderState::Canceled;
  ++canceled_order_count_;
  record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::CancelAcknowledged,
                                  exchange_order_id, it->second.client_order_id,
                                  it->second.symbol_id, it->second.side, it->second.quantity});
  return true;
}

bool SimulatedBrokerSession::reject_cancel(OrderId exchange_order_id, TimestampNs timestamp_ns) {
  const auto it = orders_.find(exchange_order_id);
  if (it == orders_.end() || it->second.state != BrokerOrderState::PendingCancel) {
    return false;
  }
  it->second.state = BrokerOrderState::Accepted;
  ++cancel_reject_count_;
  record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::CancelRejected, exchange_order_id,
                                  it->second.client_order_id, it->second.symbol_id,
                                  it->second.side, it->second.quantity});
  return true;
}

bool SimulatedBrokerSession::on_order_filled(OrderId exchange_order_id, TimestampNs timestamp_ns) {
  const auto it = orders_.find(exchange_order_id);
  if (it == orders_.end() || (it->second.state != BrokerOrderState::Accepted &&
                              it->second.state != BrokerOrderState::PendingCancel)) {
    return false;
  }
  it->second.state = BrokerOrderState::Filled;
  ++filled_order_count_;
  record_event(BrokerSessionEvent{timestamp_ns, BrokerEventType::OrderFilled, exchange_order_id,
                                  it->second.client_order_id, it->second.symbol_id,
                                  it->second.side, it->second.quantity});
  return true;
}

std::size_t SimulatedBrokerSession::pending_cancel_count() const noexcept {
  std::size_t count = 0;
  for (const auto& [order_id, record] : orders_) {
    if (record.state == BrokerOrderState::PendingCancel) {
      ++count;
    }
  }
  return count;
}

std::size_t SimulatedBrokerSession::live_order_count() const noexcept {
  std::size_t count = 0;
  for (const auto& [order_id, record] : orders_) {
    if (record.state == BrokerOrderState::Accepted ||
        record.state == BrokerOrderState::PendingCancel) {
      ++count;
    }
  }
  return count;
}

bool SimulatedBrokerSession::has_order(OrderId exchange_order_id) const {
  return orders_.find(exchange_order_id) != orders_.end();
}

BrokerOrderState SimulatedBrokerSession::order_state(OrderId exchange_order_id) const {
  const auto it = orders_.find(exchange_order_id);
  return it == orders_.end() ? BrokerOrderState::Canceled : it->second.state;
}

BrokerSessionSnapshot SimulatedBrokerSession::snapshot() const {
  BrokerSessionSnapshot snapshot;
  snapshot.connection_state = state_;
  snapshot.connect_count = connect_count_;
  snapshot.disconnect_count = disconnect_count_;
  snapshot.reconnect_count = reconnect_count_;
  snapshot.live_order_count = live_order_count();
  snapshot.pending_cancel_count = pending_cancel_count();
  snapshot.filled_order_count = filled_order_count_;
  snapshot.canceled_order_count = canceled_order_count_;
  snapshot.cancel_reject_count = cancel_reject_count_;
  snapshot.event_count = events_.size();
  snapshot.event_checksum = event_checksum_;
  return snapshot;
}

} // namespace asterion
