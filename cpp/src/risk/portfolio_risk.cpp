#include "asterion/risk/portfolio_risk.hpp"

#include "asterion/core/checksum.hpp"

#include <algorithm>
#include <string>
#include <utility>

namespace asterion {

namespace {

[[nodiscard]] std::int64_t abs_i64(std::int64_t value) noexcept {
  return value < 0 ? -value : value;
}

[[nodiscard]] RejectReason reject_reason_for(PortfolioBreach breach) noexcept {
  switch (breach) {
  case PortfolioBreach::GrossExposure:
    return RejectReason::MaxPortfolioGrossExposure;
  case PortfolioBreach::NetExposure:
    return RejectReason::MaxPortfolioNetExposure;
  case PortfolioBreach::Concentration:
    return RejectReason::MaxSymbolConcentration;
  case PortfolioBreach::Loss:
    return RejectReason::MaxPortfolioLoss;
  case PortfolioBreach::None:
    return RejectReason::None;
  }
  return RejectReason::None;
}

} // namespace

std::string_view to_string(PortfolioBreach breach) noexcept {
  switch (breach) {
  case PortfolioBreach::None:
    return "none";
  case PortfolioBreach::GrossExposure:
    return "gross_exposure";
  case PortfolioBreach::NetExposure:
    return "net_exposure";
  case PortfolioBreach::Concentration:
    return "concentration";
  case PortfolioBreach::Loss:
    return "loss";
  }
  return "unknown";
}

std::string_view portfolio_check_name(PortfolioBreach breach) noexcept {
  switch (breach) {
  case PortfolioBreach::None:
    return "portfolio_accepted";
  case PortfolioBreach::GrossExposure:
    return "portfolio_gross_exposure";
  case PortfolioBreach::NetExposure:
    return "portfolio_net_exposure";
  case PortfolioBreach::Concentration:
    return "portfolio_concentration";
  case PortfolioBreach::Loss:
    return "portfolio_loss";
  }
  return "portfolio_unknown";
}

bool PortfolioRiskMonitor::active() const noexcept {
  return limits_.max_gross_exposure_ticks > 0 || limits_.max_net_exposure_ticks > 0 ||
         limits_.max_symbol_concentration_bps > 0 || limits_.max_loss_ticks > 0;
}

PriceTicks PortfolioRiskMonitor::mark_of(const SymbolPosition& position) noexcept {
  return position.has_mark ? position.mark : position.average_cost;
}

void PortfolioRiskMonitor::set_mark(SymbolId symbol_id, PriceTicks mark_ticks) {
  SymbolPosition& position = positions_[symbol_id];
  position.mark = mark_ticks;
  position.has_mark = true;
}

void PortfolioRiskMonitor::set_position(SymbolId symbol_id, Quantity signed_quantity,
                                        PriceTicks average_cost_ticks) {
  SymbolPosition& position = positions_[symbol_id];
  position.quantity = signed_quantity;
  position.average_cost = average_cost_ticks;
}

void PortfolioRiskMonitor::apply_fill_to(SymbolPosition& position, std::int64_t& realised_pnl,
                                         Side side, Quantity quantity, PriceTicks price_ticks) {
  if (quantity <= 0 || !is_valid_side(side)) {
    return;
  }
  const Quantity signed_qty = side == Side::Buy ? quantity : -quantity;
  const Quantity current = position.quantity;

  const bool extending = (current >= 0 && signed_qty > 0) || (current <= 0 && signed_qty < 0);
  if (extending) {
    const Quantity new_qty = current + signed_qty;
    const std::int64_t total_cost =
        position.average_cost * abs_i64(current) + price_ticks * quantity;
    position.average_cost = new_qty == 0 ? 0 : total_cost / abs_i64(new_qty);
    position.quantity = new_qty;
    return;
  }

  // Reducing, closing or flipping the position.
  const Quantity closing_qty = std::min<Quantity>(quantity, abs_i64(current));
  if (current > 0) {
    realised_pnl += (price_ticks - position.average_cost) * closing_qty;
  } else {
    realised_pnl += (position.average_cost - price_ticks) * closing_qty;
  }
  const Quantity new_signed = current + signed_qty;
  position.quantity = new_signed;
  if (new_signed == 0) {
    position.average_cost = 0;
  } else if ((current > 0 && new_signed < 0) || (current < 0 && new_signed > 0)) {
    position.average_cost = price_ticks; // flipped: remainder opens at the fill price
  }
}

void PortfolioRiskMonitor::apply_fill(SymbolId symbol_id, Side side, Quantity quantity,
                                      PriceTicks price_ticks) {
  apply_fill_to(positions_[symbol_id], realised_pnl_, side, quantity, price_ticks);
}

Quantity PortfolioRiskMonitor::position(SymbolId symbol_id) const noexcept {
  const auto it = positions_.find(symbol_id);
  return it == positions_.end() ? 0 : it->second.quantity;
}

std::int64_t PortfolioRiskMonitor::gross_exposure_ticks() const noexcept {
  std::int64_t gross = 0;
  for (const auto& [symbol_id, position] : positions_) {
    gross += abs_i64(position.quantity) * mark_of(position);
  }
  return gross;
}

std::int64_t PortfolioRiskMonitor::net_exposure_ticks() const noexcept {
  std::int64_t net = 0;
  for (const auto& [symbol_id, position] : positions_) {
    net += position.quantity * mark_of(position);
  }
  return abs_i64(net);
}

std::int64_t PortfolioRiskMonitor::unrealised_pnl_ticks() const noexcept {
  std::int64_t pnl = 0;
  for (const auto& [symbol_id, position] : positions_) {
    pnl += (mark_of(position) - position.average_cost) * position.quantity;
  }
  return pnl;
}

std::int64_t PortfolioRiskMonitor::total_pnl_ticks() const noexcept {
  return realised_pnl_ + unrealised_pnl_ticks();
}

PortfolioCheckResult PortfolioRiskMonitor::evaluate(
    const std::map<SymbolId, SymbolPosition>& positions, std::int64_t realised_pnl) const {
  std::int64_t gross = 0;
  std::int64_t net = 0;
  std::int64_t unrealised = 0;
  for (const auto& [symbol_id, position] : positions) {
    const PriceTicks mark = mark_of(position);
    gross += abs_i64(position.quantity) * mark;
    net += position.quantity * mark;
    unrealised += (mark - position.average_cost) * position.quantity;
  }
  net = abs_i64(net);
  const std::int64_t total_pnl = realised_pnl + unrealised;

  if (limits_.max_gross_exposure_ticks > 0 && gross > limits_.max_gross_exposure_ticks) {
    return PortfolioCheckResult{false, PortfolioBreach::GrossExposure, kInvalidSymbolId,
                                limits_.max_gross_exposure_ticks, gross};
  }
  if (limits_.max_net_exposure_ticks > 0 && net > limits_.max_net_exposure_ticks) {
    return PortfolioCheckResult{false, PortfolioBreach::NetExposure, kInvalidSymbolId,
                                limits_.max_net_exposure_ticks, net};
  }
  if (limits_.max_symbol_concentration_bps > 0 && gross > 0) {
    SymbolId worst_symbol = kInvalidSymbolId;
    std::int64_t worst_bps = 0;
    for (const auto& [symbol_id, position] : positions) {
      const std::int64_t symbol_gross = abs_i64(position.quantity) * mark_of(position);
      const std::int64_t bps = symbol_gross * 10'000 / gross;
      if (bps > worst_bps) {
        worst_bps = bps;
        worst_symbol = symbol_id;
      }
    }
    if (worst_bps > limits_.max_symbol_concentration_bps) {
      return PortfolioCheckResult{false, PortfolioBreach::Concentration, worst_symbol,
                                  limits_.max_symbol_concentration_bps, worst_bps};
    }
  }
  if (limits_.max_loss_ticks > 0 && total_pnl <= -limits_.max_loss_ticks) {
    return PortfolioCheckResult{false, PortfolioBreach::Loss, kInvalidSymbolId,
                                limits_.max_loss_ticks, -total_pnl};
  }
  return PortfolioCheckResult{true, PortfolioBreach::None, kInvalidSymbolId, 0, 0};
}

PortfolioCheckResult PortfolioRiskMonitor::evaluate_order(SymbolId symbol_id, Side side,
                                                          Quantity quantity,
                                                          PriceTicks price_ticks) const {
  std::map<SymbolId, SymbolPosition> hypothetical = positions_;
  std::int64_t realised = realised_pnl_;
  apply_fill_to(hypothetical[symbol_id], realised, side, quantity, price_ticks);
  return evaluate(hypothetical, realised);
}

PortfolioCheckResult PortfolioRiskMonitor::evaluate_state() const {
  return evaluate(positions_, realised_pnl_);
}

PortfolioCheckResult PortfolioRiskMonitor::check_order(SymbolId symbol_id, Side side,
                                                       Quantity quantity, PriceTicks price_ticks,
                                                       TimestampNs now_ns) {
  const PortfolioCheckResult result = evaluate_order(symbol_id, side, quantity, price_ticks);
  if (record_audit_) {
    audit_.record(RiskAuditEntry{now_ns, kInvalidClientOrderId, symbol_id, side, result.accepted,
                                 reject_reason_for(result.breach),
                                 std::string(portfolio_check_name(result.breach)),
                                 result.limit_value, result.observed_value});
  }
  return result;
}

PortfolioSnapshot PortfolioRiskMonitor::snapshot() const {
  PortfolioSnapshot snapshot;
  snapshot.gross_exposure_ticks = gross_exposure_ticks();
  snapshot.net_exposure_ticks = net_exposure_ticks();
  snapshot.realised_pnl_ticks = realised_pnl_;
  snapshot.unrealised_pnl_ticks = unrealised_pnl_ticks();
  snapshot.total_pnl_ticks = snapshot.realised_pnl_ticks + snapshot.unrealised_pnl_ticks;
  snapshot.symbol_count = positions_.size();

  if (snapshot.gross_exposure_ticks > 0) {
    for (const auto& [symbol_id, position] : positions_) {
      const std::int64_t symbol_gross = abs_i64(position.quantity) * mark_of(position);
      const std::int64_t bps = symbol_gross * 10'000 / snapshot.gross_exposure_ticks;
      if (bps > snapshot.max_concentration_bps) {
        snapshot.max_concentration_bps = bps;
        snapshot.max_concentration_symbol = symbol_id;
      }
    }
  }

  std::uint64_t seed = kFnvOffsetBasis;
  seed = checksum_append(seed, limits_.max_gross_exposure_ticks);
  seed = checksum_append(seed, limits_.max_net_exposure_ticks);
  seed = checksum_append(seed, limits_.max_symbol_concentration_bps);
  seed = checksum_append(seed, limits_.max_loss_ticks);
  seed = checksum_append(seed, realised_pnl_);
  seed = checksum_append(seed, static_cast<std::uint64_t>(positions_.size()));
  for (const auto& [symbol_id, position] : positions_) {
    seed = checksum_append(seed, symbol_id);
    seed = checksum_append(seed, position.quantity);
    seed = checksum_append(seed, position.average_cost);
    seed = checksum_append(seed, position.mark);
    seed = checksum_append(seed, static_cast<std::uint8_t>(position.has_mark ? 1U : 0U));
  }
  snapshot.checksum = seed;
  return snapshot;
}

} // namespace asterion
