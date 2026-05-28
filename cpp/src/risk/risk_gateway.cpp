#include "asterion/risk/risk_gateway.hpp"

#include <cstdlib>

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

RiskResult RiskGateway::check_new_order(const NewOrderRequest& request, TimestampNs now_ns) {
  if (kill_switch_.enabled()) {
    return RiskResult{false, RejectReason::KillSwitch};
  }
  if (request.quantity <= 0) {
    return RiskResult{false, RejectReason::InvalidQuantity};
  }
  if (request.order_type == OrderType::Limit && request.price_ticks <= 0) {
    return RiskResult{false, RejectReason::InvalidPrice};
  }
  if (accepted_client_order_ids_.find(request.client_order_id) !=
      accepted_client_order_ids_.end()) {
    return RiskResult{false, RejectReason::DuplicateClientOrderId};
  }
  if (request.quantity > limits_.max_order_quantity) {
    return RiskResult{false, RejectReason::MaxOrderQuantity};
  }

  const auto market_it = market_state_.find(request.symbol_id);
  if (market_it == market_state_.end()) {
    return RiskResult{false, RejectReason::StaleMarketData};
  }
  const MarketState market_state = market_it->second;
  if (limits_.stale_after_ns > 0 && now_ns - market_state.last_timestamp_ns > limits_.stale_after_ns) {
    return RiskResult{false, RejectReason::StaleMarketData};
  }

  if (request.order_type == OrderType::Limit &&
      abs_i64(request.price_ticks - market_state.reference_price_ticks) > limits_.price_band_ticks) {
    return RiskResult{false, RejectReason::PriceBand};
  }

  if (notional_for(request) > limits_.max_notional_ticks) {
    return RiskResult{false, RejectReason::MaxNotional};
  }

  const Quantity signed_delta = request.side == Side::Buy ? request.quantity : -request.quantity;
  const Quantity projected_position = position(request.symbol_id) + signed_delta;
  if (abs_i64(projected_position) > limits_.max_position_per_symbol) {
    return RiskResult{false, RejectReason::MaxPosition};
  }

  if (gross_exposure_with(request) > limits_.max_gross_exposure_ticks) {
    return RiskResult{false, RejectReason::MaxGrossExposure};
  }

  accepted_client_order_ids_.insert(request.client_order_id);
  return RiskResult{true, RejectReason::None};
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
