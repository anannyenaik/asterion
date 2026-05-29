#pragma once

#include "asterion/core/checksum.hpp"
#include "asterion/core/types.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace asterion {

// Simulated broker/session lifecycle. This is a deterministic in-process state
// machine, not a real venue connection: it never sends network messages. It models
// the order/session events a broker session would surface so cancel-on-disconnect
// and pending-cancel handling can be tested deterministically.
//
// When attached to a RiskGateway, a disconnect with cancel-on-disconnect requests
// cancels for every live order and releases the gateway's tracked simulated working
// exposure through RiskGateway::on_disconnect. The session keeps its own append-only
// event log with a deterministic FNV-1a checksum so a lifecycle is a reproducible
// artifact, the same property the book and risk audit checksums provide.

enum class SessionConnectionState : std::uint8_t { Disconnected = 1, Connected = 2 };

enum class BrokerOrderState : std::uint8_t {
  Accepted = 1,
  PendingCancel = 2,
  Canceled = 3,
  Filled = 4,
};

enum class BrokerEventType : std::uint8_t {
  Connect = 1,
  Disconnect = 2,
  Reconnect = 3,
  OrderAccepted = 4,
  CancelRequested = 5,
  CancelAcknowledged = 6,
  CancelRejected = 7,
  OrderFilled = 8,
};

[[nodiscard]] std::string_view to_string(SessionConnectionState state) noexcept;
[[nodiscard]] std::string_view to_string(BrokerOrderState state) noexcept;
[[nodiscard]] std::string_view to_string(BrokerEventType type) noexcept;

struct BrokerSessionEvent {
  TimestampNs timestamp_ns{0};
  BrokerEventType type{BrokerEventType::Connect};
  // Order this event refers to; kInvalidOrderId for pure session events
  // (connect/disconnect/reconnect).
  OrderId exchange_order_id{kInvalidOrderId};
  ClientOrderId client_order_id{kInvalidClientOrderId};
  SymbolId symbol_id{kInvalidSymbolId};
  Side side{Side::None};
  Quantity quantity{0};
};

[[nodiscard]] std::uint64_t append_to_checksum(std::uint64_t seed,
                                               const BrokerSessionEvent& event) noexcept;
[[nodiscard]] std::uint64_t checksum_broker_session_events(
    std::span<const BrokerSessionEvent> events) noexcept;

struct BrokerSessionSnapshot {
  SessionConnectionState connection_state{SessionConnectionState::Disconnected};
  std::size_t connect_count{0};
  std::size_t disconnect_count{0};
  std::size_t reconnect_count{0};
  std::size_t live_order_count{0};      // Accepted or PendingCancel
  std::size_t pending_cancel_count{0};  // PendingCancel only
  std::size_t filled_order_count{0};
  std::size_t canceled_order_count{0};
  std::size_t cancel_reject_count{0};
  std::size_t event_count{0};
  std::uint64_t event_checksum{0};
};

class SimulatedBrokerSession {
public:
  SimulatedBrokerSession() = default;
  // Whether a disconnect should request cancels for all live orders. Mirrors the
  // gateway's cancel_on_disconnect so the two stay consistent.
  explicit SimulatedBrokerSession(bool cancel_on_disconnect)
      : cancel_on_disconnect_(cancel_on_disconnect) {}

  // Optional gateway whose simulated working exposure is released on disconnect.
  // The session does not take ownership; the gateway must outlive the session.
  void attach_risk_gateway(RiskGateway* gateway) noexcept { gateway_ = gateway; }
  void set_cancel_on_disconnect(bool enabled) noexcept { cancel_on_disconnect_ = enabled; }
  [[nodiscard]] bool cancel_on_disconnect() const noexcept { return cancel_on_disconnect_; }

  void connect(TimestampNs timestamp_ns);
  void disconnect(TimestampNs timestamp_ns);
  void reconnect(TimestampNs timestamp_ns);

  // Lifecycle transitions. Each returns false when the transition is not valid for
  // the order's current state (e.g. acknowledging a cancel that was never
  // requested, or a duplicate accept), leaving state unchanged.
  [[nodiscard]] bool on_order_accepted(OrderId exchange_order_id, ClientOrderId client_order_id,
                                       SymbolId symbol_id, Side side, Quantity quantity,
                                       TimestampNs timestamp_ns);
  [[nodiscard]] bool request_cancel(OrderId exchange_order_id, TimestampNs timestamp_ns);
  [[nodiscard]] bool acknowledge_cancel(OrderId exchange_order_id, TimestampNs timestamp_ns);
  [[nodiscard]] bool reject_cancel(OrderId exchange_order_id, TimestampNs timestamp_ns);
  [[nodiscard]] bool on_order_filled(OrderId exchange_order_id, TimestampNs timestamp_ns);

  [[nodiscard]] SessionConnectionState connection_state() const noexcept { return state_; }
  [[nodiscard]] bool connected() const noexcept {
    return state_ == SessionConnectionState::Connected;
  }
  [[nodiscard]] std::size_t pending_cancel_count() const noexcept;
  [[nodiscard]] std::size_t live_order_count() const noexcept;
  [[nodiscard]] bool has_order(OrderId exchange_order_id) const;
  [[nodiscard]] BrokerOrderState order_state(OrderId exchange_order_id) const;
  [[nodiscard]] const std::vector<BrokerSessionEvent>& events() const noexcept { return events_; }
  [[nodiscard]] std::uint64_t event_checksum() const noexcept { return event_checksum_; }
  [[nodiscard]] BrokerSessionSnapshot snapshot() const;

private:
  struct OrderRecord {
    ClientOrderId client_order_id{kInvalidClientOrderId};
    SymbolId symbol_id{kInvalidSymbolId};
    Side side{Side::None};
    Quantity quantity{0};
    BrokerOrderState state{BrokerOrderState::Accepted};
  };

  void record_event(BrokerSessionEvent event);

  RiskGateway* gateway_{nullptr};
  bool cancel_on_disconnect_{false};
  SessionConnectionState state_{SessionConnectionState::Disconnected};
  std::size_t connect_count_{0};
  std::size_t disconnect_count_{0};
  std::size_t reconnect_count_{0};
  std::size_t filled_order_count_{0};
  std::size_t canceled_order_count_{0};
  std::size_t cancel_reject_count_{0};
  std::unordered_map<OrderId, OrderRecord> orders_;
  std::vector<BrokerSessionEvent> events_;
  std::uint64_t event_checksum_{kFnvOffsetBasis};
};

} // namespace asterion
