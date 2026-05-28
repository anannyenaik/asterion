#include "asterion/risk/risk_gateway.hpp"

#include <cstdlib>
#include <utility>

namespace asterion {

namespace {

[[nodiscard]] std::int64_t abs_i64(std::int64_t value) noexcept {
  return value < 0 ? -value : value;
}

} // namespace

RiskGateway::RiskGateway(RiskLimits limits) : limits_(limits) {}

void RiskGateway::on_market_data(SymbolId symbol_id, PriceTicks reference_price_ticks,
                                 TimestampNs timestamp_ns) {
  market_state_[symbol_id] = MarketState{reference_price_ticks, timestamp_ns};
}

void RiskGateway::set_position(SymbolId symbol_id, Quantity signed_position) {
  positions_[symbol_id] = signed_position;
}

Quantity RiskGateway::position(SymbolId symbol_id) const noexcept {
  const auto it = positions_.find(symbol_id);
  return it == positions_.end() ? 0 : it->second;
}

RiskResult RiskGateway::decide(const NewOrderRequest& request, TimestampNs now_ns,
                               std::string check_name, bool accepted, RejectReason reason,
                               std::int64_t limit_value, std::int64_t observed_value) {
  audit_.record(RiskAuditEntry{now_ns, request.client_order_id, request.symbol_id, request.side,
                               accepted, reason, std::move(check_name), limit_value,
                               observed_value});
  return RiskResult{accepted, reason};
}

RiskResult RiskGateway::check_new_order(const NewOrderRequest& request, TimestampNs now_ns) {
  if (kill_switch_.enabled()) {
    return decide(request, now_ns, "kill_switch", false, RejectReason::KillSwitch, 0, 0);
  }
  if (request.quantity <= 0) {
    return decide(request, now_ns, "invalid_quantity", false, RejectReason::InvalidQuantity, 0,
                  request.quantity);
  }
  if (request.order_type == OrderType::Limit && request.price_ticks <= 0) {
    return decide(request, now_ns, "invalid_price", false, RejectReason::InvalidPrice, 0,
                  request.price_ticks);
  }
  if (accepted_client_order_ids_.find(request.client_order_id) !=
      accepted_client_order_ids_.end()) {
    return decide(request, now_ns, "duplicate_client_order_id", false,
                  RejectReason::DuplicateClientOrderId, 0,
                  static_cast<std::int64_t>(request.client_order_id));
  }
  if (request.quantity > limits_.max_order_quantity) {
    return decide(request, now_ns, "max_order_quantity", false, RejectReason::MaxOrderQuantity,
                  limits_.max_order_quantity, request.quantity);
  }

  const auto market_it = market_state_.find(request.symbol_id);
  if (market_it == market_state_.end()) {
    return decide(request, now_ns, "stale_market_data", false, RejectReason::StaleMarketData,
                  limits_.stale_after_ns, 0);
  }
  const MarketState market_state = market_it->second;
  const TimestampNs age_ns = now_ns - market_state.last_timestamp_ns;
  if (limits_.stale_after_ns > 0 && age_ns > limits_.stale_after_ns) {
    return decide(request, now_ns, "stale_market_data", false, RejectReason::StaleMarketData,
                  limits_.stale_after_ns, age_ns);
  }

  if (request.order_type == OrderType::Limit &&
      abs_i64(request.price_ticks - market_state.reference_price_ticks) > limits_.price_band_ticks) {
    return decide(request, now_ns, "price_band", false, RejectReason::PriceBand,
                  limits_.price_band_ticks,
                  abs_i64(request.price_ticks - market_state.reference_price_ticks));
  }

  const std::int64_t notional = notional_for(request);
  if (notional > limits_.max_notional_ticks) {
    return decide(request, now_ns, "max_notional", false, RejectReason::MaxNotional,
                  limits_.max_notional_ticks, notional);
  }

  const Quantity signed_delta = request.side == Side::Buy ? request.quantity : -request.quantity;
  const Quantity projected_position = position(request.symbol_id) + signed_delta;
  if (abs_i64(projected_position) > limits_.max_position_per_symbol) {
    return decide(request, now_ns, "max_position", false, RejectReason::MaxPosition,
                  limits_.max_position_per_symbol, abs_i64(projected_position));
  }

  const std::int64_t gross_exposure = gross_exposure_with(request);
  if (gross_exposure > limits_.max_gross_exposure_ticks) {
    return decide(request, now_ns, "max_gross_exposure", false, RejectReason::MaxGrossExposure,
                  limits_.max_gross_exposure_ticks, gross_exposure);
  }

  accepted_client_order_ids_.insert(request.client_order_id);
  return decide(request, now_ns, "accepted", true, RejectReason::None,
                limits_.max_notional_ticks, notional);
}

std::int64_t RiskGateway::notional_for(const NewOrderRequest& request) const noexcept {
  PriceTicks price = request.price_ticks;
  if (request.order_type == OrderType::Market) {
    const auto market_it = market_state_.find(request.symbol_id);
    price = market_it == market_state_.end() ? 0 : market_it->second.reference_price_ticks;
  }
  return abs_i64(price * request.quantity);
}

std::int64_t RiskGateway::gross_exposure_with(const NewOrderRequest& request) const noexcept {
  std::int64_t gross = notional_for(request);
  for (const auto& [symbol_id, signed_position] : positions_) {
    const auto market_it = market_state_.find(symbol_id);
    if (market_it == market_state_.end()) {
      continue;
    }
    gross += abs_i64(signed_position) * market_it->second.reference_price_ticks;
  }
  return gross;
}

} // namespace asterion
