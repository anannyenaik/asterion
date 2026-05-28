#pragma once

#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/kill_switch.hpp"
#include "asterion/risk/limits.hpp"
#include "asterion/risk/risk_audit.hpp"

#include <cstdint>
#include <functional>
#include <map>
#include <string_view>
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

  // Working (open) order lifecycle. An accepted limit order is registered as a
  // resting order so the gateway can enforce open-order exposure and self-trade
  // prevention; call release_order once it is fully filled or cancelled. These
  // are no-ops unless working-order tracking is enabled by the limits.
  void release_order(ClientOrderId client_order_id);
  [[nodiscard]] Quantity working_quantity(SymbolId symbol_id) const noexcept;

  // Audit recording is opt-in. It allocates (a per-entry string and the trail
  // vector), so it is disabled by default to keep the pre-trade hot path
  // allocation-free; enable it explicitly when an audit trail is wanted.
  void set_audit_enabled(bool enabled) noexcept { record_audit_ = enabled; }
  [[nodiscard]] bool audit_enabled() const noexcept { return record_audit_; }
  [[nodiscard]] const RiskAuditTrail& audit() const noexcept { return audit_; }
  void clear_audit() noexcept { audit_.clear(); }

private:
  struct MarketState {
    PriceTicks reference_price_ticks{0};
    TimestampNs last_timestamp_ns{0};
  };

  struct WorkingOrder {
    ClientId client_id{0};
    SymbolId symbol_id{kInvalidSymbolId};
    Side side{Side::None};
    PriceTicks price_ticks{0};
    Quantity quantity{0};
  };

  // Per (client, symbol) resting quantity keyed by price. Bids are ordered so the
  // most aggressive (highest) price is first; asks so the lowest price is first.
  struct ClientWorkingBook {
    std::map<PriceTicks, Quantity, std::greater<>> buy_qty_by_price;
    std::map<PriceTicks, Quantity> sell_qty_by_price;
  };

  // Fixed-window message-rate state per client.
  struct RateState {
    TimestampNs window_start_ns{0};
    std::uint32_t count{0};
    bool active{false};
  };

  [[nodiscard]] bool tracks_working_orders() const noexcept {
    return limits_.max_open_order_quantity > 0 || limits_.enable_self_trade_prevention;
  }
  [[nodiscard]] static std::uint64_t client_symbol_key(ClientId client_id,
                                                       SymbolId symbol_id) noexcept;
  void register_working_order(const NewOrderRequest& request);

  [[nodiscard]] std::int64_t notional_for(const NewOrderRequest& request) const noexcept;
  [[nodiscard]] std::int64_t gross_exposure_with(const NewOrderRequest& request) const noexcept;
  [[nodiscard]] RiskResult decide(const NewOrderRequest& request, TimestampNs now_ns,
                                  std::string_view check_name, bool accepted, RejectReason reason,
                                  std::int64_t limit_value, std::int64_t observed_value);

  RiskLimits limits_;
  KillSwitch kill_switch_;
  std::unordered_set<ClientOrderId> accepted_client_order_ids_;
  std::unordered_map<SymbolId, MarketState> market_state_;
  std::unordered_map<SymbolId, Quantity> positions_;
  std::unordered_map<SymbolId, Quantity> working_quantity_;
  std::unordered_map<ClientOrderId, WorkingOrder> working_orders_;
  std::unordered_map<std::uint64_t, ClientWorkingBook> client_books_;
  std::unordered_map<ClientId, RateState> rate_states_;
  RiskAuditTrail audit_;
  bool record_audit_{false};
};

} // namespace asterion
