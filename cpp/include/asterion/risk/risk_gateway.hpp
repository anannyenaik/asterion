#pragma once

#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/kill_switch.hpp"
#include "asterion/risk/limits.hpp"
#include "asterion/risk/risk_audit.hpp"

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace asterion {

struct RiskResult {
  bool accepted{false};
  RejectReason reject_reason{RejectReason::None};
};

class RiskGateway {
public:
  explicit RiskGateway(RiskLimits limits = {});

  void set_limits(RiskLimits limits) noexcept { limits_ = limits; }
  [[nodiscard]] const RiskLimits& limits() const noexcept { return limits_; }

  void enable_kill_switch() noexcept { kill_switch_.enable(); }
  void disable_kill_switch() noexcept { kill_switch_.disable(); }
  [[nodiscard]] bool kill_switch_enabled() const noexcept { return kill_switch_.enabled(); }

  void on_market_data(SymbolId symbol_id, PriceTicks reference_price_ticks,
                      TimestampNs timestamp_ns);
  void set_position(SymbolId symbol_id, Quantity signed_position);
  [[nodiscard]] Quantity position(SymbolId symbol_id) const noexcept;

  [[nodiscard]] RiskResult check_new_order(const NewOrderRequest& request, TimestampNs now_ns);

  [[nodiscard]] const RiskAuditTrail& audit() const noexcept { return audit_; }
  void clear_audit() noexcept { audit_.clear(); }

private:
  struct MarketState {
    PriceTicks reference_price_ticks{0};
    TimestampNs last_timestamp_ns{0};
  };

  [[nodiscard]] std::int64_t notional_for(const NewOrderRequest& request) const noexcept;
  [[nodiscard]] std::int64_t gross_exposure_with(const NewOrderRequest& request) const noexcept;
  [[nodiscard]] RiskResult decide(const NewOrderRequest& request, TimestampNs now_ns,
                                  std::string check_name, bool accepted, RejectReason reason,
                                  std::int64_t limit_value, std::int64_t observed_value);

  RiskLimits limits_;
  KillSwitch kill_switch_;
  std::unordered_set<ClientOrderId> accepted_client_order_ids_;
  std::unordered_map<SymbolId, MarketState> market_state_;
  std::unordered_map<SymbolId, Quantity> positions_;
  RiskAuditTrail audit_;
};

} // namespace asterion
