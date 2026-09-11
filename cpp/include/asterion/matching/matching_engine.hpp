#pragma once

#include "asterion/book/order_book.hpp"
#include "asterion/matching/order_state.hpp"

#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace asterion {

struct NewOrderRequest {
  ClientOrderId client_order_id{kInvalidClientOrderId};
  SymbolId symbol_id{kInvalidSymbolId};
  Side side{Side::None};
  OrderType order_type{OrderType::Limit};
  PriceTicks price_ticks{0};
  Quantity quantity{0};
  TimestampNs timestamp_ns{0};
  // Originating strategy/client identifier used by message-rate limiting and
  // self-trade prevention. Defaults to 0 (unattributed).
  ClientId client_id{0};
  TimeInForce time_in_force{TimeInForce::Gtc};
  bool post_only{false};
};

struct CancelOrderRequest {
  ClientOrderId client_order_id{kInvalidClientOrderId};
  OrderId exchange_order_id{kInvalidOrderId};
  TimestampNs timestamp_ns{0};
};

struct ReplaceOrderRequest {
  ClientOrderId client_order_id{kInvalidClientOrderId};
  OrderId exchange_order_id{kInvalidOrderId};
  PriceTicks new_price_ticks{0};
  Quantity new_quantity{0};
  TimestampNs timestamp_ns{0};
};

class MatchingEngine {
public:
  explicit MatchingEngine(SymbolId symbol_id);

  [[nodiscard]] const OrderBook& book() const noexcept { return book_; }
  [[nodiscard]] std::uint64_t reports_checksum() const noexcept { return reports_checksum_; }

  [[nodiscard]] std::vector<ExecutionReport> submit_order(const NewOrderRequest& request);
  [[nodiscard]] std::vector<ExecutionReport> cancel_order(const CancelOrderRequest& request);
  [[nodiscard]] std::vector<ExecutionReport> replace_order(const ReplaceOrderRequest& request);

private:
  [[nodiscard]] bool crosses(Side incoming_side, OrderType order_type,
                             PriceTicks limit_price_ticks) const;
  [[nodiscard]] bool can_fully_fill(const NewOrderRequest& request) const;
  [[nodiscard]] bool would_self_trade(Side incoming_side, OrderType order_type,
                                      PriceTicks limit_price_ticks, ClientId client_id,
                                      OrderId excluded_order_id = kInvalidOrderId) const;
  void match_against_book(OrderState& incoming, Quantity& remaining, TimestampNs timestamp_ns,
                          std::vector<ExecutionReport>& reports);
  [[nodiscard]] bool rest_limit_order(const OrderState& state, Quantity remaining,
                                      TimestampNs timestamp_ns);

  [[nodiscard]] ExecutionReport make_report(const OrderState& state, ExecType exec_type,
                                            Quantity last_fill_quantity,
                                            PriceTicks last_fill_price_ticks,
                                            TimestampNs timestamp_ns,
                                            RejectReason reject_reason) const;
  [[nodiscard]] ExecutionReport make_reject(ClientOrderId client_order_id, SymbolId symbol_id,
                                            Side side, TimestampNs timestamp_ns,
                                            RejectReason reason) const;
  void record_report(std::vector<ExecutionReport>& reports, ExecutionReport report);
  [[nodiscard]] bool reserve_client_order_id(ClientOrderId client_order_id);

  SymbolId symbol_id_;
  OrderBook book_;
  OrderId next_exchange_order_id_{1};
  std::unordered_map<OrderId, OrderState> states_;
  std::unordered_map<ClientOrderId, OrderId> client_to_exchange_;
  std::unordered_set<ClientOrderId> seen_client_order_ids_;
  std::uint64_t reports_checksum_;
};

} // namespace asterion
