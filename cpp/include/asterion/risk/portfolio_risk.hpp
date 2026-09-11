#pragma once

#include "asterion/core/types.hpp"
#include "asterion/risk/risk_audit.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <string_view>

namespace asterion {

// Portfolio-level risk monitor. Tracks per-symbol signed position, average entry
// cost and realised PnL, marks each symbol with a caller-provided simulated price,
// and evaluates portfolio-wide limits. It is integer-only and deterministic (no
// wall clock), so its snapshot checksum and audit trail are reproducible.
//
// This is a simulation/accounting layer: the marks are caller-supplied simulated
// prices, not live market data, and "loss" is computed from those simulated marks.
// It does not replace the per-order RiskGateway; it is a separate portfolio gate.

struct PortfolioRiskLimits {
  // All limits default to 0 = disabled, so an unconfigured monitor enforces nothing
  // and is a no-op gate.
  std::int64_t max_gross_exposure_ticks{0};      // sum of |position_i| * mark_i
  std::int64_t max_net_exposure_ticks{0};        // |sum of position_i * mark_i|
  std::int64_t max_symbol_concentration_bps{0};  // max single-symbol share of gross, basis points
  std::int64_t max_loss_ticks{0};                // reject when total PnL <= -max_loss_ticks
};

enum class PortfolioBreach : std::uint8_t {
  None = 0,
  GrossExposure = 1,
  NetExposure = 2,
  Concentration = 3,
  Loss = 4,
};

[[nodiscard]] std::string_view to_string(PortfolioBreach breach) noexcept;
// The risk-audit check name recorded for a breach (or acceptance).
[[nodiscard]] std::string_view portfolio_check_name(PortfolioBreach breach) noexcept;

struct PortfolioCheckResult {
  bool accepted{true};
  PortfolioBreach breach{PortfolioBreach::None};
  SymbolId symbol_id{kInvalidSymbolId};
  std::int64_t limit_value{0};
  std::int64_t observed_value{0};
};

struct PortfolioSnapshot {
  std::int64_t gross_exposure_ticks{0};
  std::int64_t net_exposure_ticks{0};
  std::int64_t realised_pnl_ticks{0};
  std::int64_t unrealised_pnl_ticks{0};
  std::int64_t total_pnl_ticks{0};
  SymbolId max_concentration_symbol{kInvalidSymbolId};
  std::int64_t max_concentration_bps{0};
  std::size_t symbol_count{0};
  std::uint64_t checksum{0};
};

class PortfolioRiskMonitor {
public:
  PortfolioRiskMonitor() = default;
  explicit PortfolioRiskMonitor(PortfolioRiskLimits limits) : limits_(limits) {}

  void set_limits(PortfolioRiskLimits limits) noexcept { limits_ = limits; }
  [[nodiscard]] const PortfolioRiskLimits& limits() const noexcept { return limits_; }
  [[nodiscard]] bool active() const noexcept;

  // Simulated mark price for a symbol. Used for exposure and unrealised PnL; when a
  // symbol has no mark, its average cost is used as a fallback.
  void set_mark(SymbolId symbol_id, PriceTicks mark_ticks);
  // Apply a fill from the portfolio's perspective: a Buy increases the long (or
  // covers a short), a Sell increases the short (or reduces a long). Updates
  // position, average cost and realised PnL.
  void apply_fill(SymbolId symbol_id, Side side, Quantity quantity, PriceTicks price_ticks);
  // Seed a signed position with an average cost directly.
  void set_position(SymbolId symbol_id, Quantity signed_quantity, PriceTicks average_cost_ticks);

  [[nodiscard]] Quantity position(SymbolId symbol_id) const noexcept;
  [[nodiscard]] std::int64_t gross_exposure_ticks() const noexcept;
  [[nodiscard]] std::int64_t net_exposure_ticks() const noexcept;
  [[nodiscard]] std::int64_t realised_pnl_ticks() const noexcept { return realised_pnl_; }
  [[nodiscard]] std::int64_t unrealised_pnl_ticks() const noexcept;
  [[nodiscard]] std::int64_t total_pnl_ticks() const noexcept;

  // Pure evaluation of the portfolio limits for a prospective order applied on top
  // of current state, without mutating anything and without recording an audit
  // entry. Returns the first breach in a fixed order.
  [[nodiscard]] PortfolioCheckResult evaluate_order(SymbolId symbol_id, Side side,
                                                    Quantity quantity,
                                                    PriceTicks price_ticks) const;
  [[nodiscard]] PortfolioCheckResult evaluate_state() const;

  // Evaluate a prospective order and, when auditing is enabled, append a
  // deterministic audit entry (timestamped with the caller-provided now_ns).
  [[nodiscard]] PortfolioCheckResult check_order(SymbolId symbol_id, Side side, Quantity quantity,
                                                 PriceTicks price_ticks, TimestampNs now_ns);

  // Audit recording is opt-in, mirroring RiskGateway, so the default path does not
  // allocate audit strings.
  void set_audit_enabled(bool enabled) noexcept { record_audit_ = enabled; }
  [[nodiscard]] bool audit_enabled() const noexcept { return record_audit_; }
  [[nodiscard]] const RiskAuditTrail& audit() const noexcept { return audit_; }
  void clear_audit() noexcept { audit_.clear(); }

  [[nodiscard]] PortfolioSnapshot snapshot() const;

private:
  struct SymbolPosition {
    Quantity quantity{0};       // signed
    PriceTicks average_cost{0}; // average entry price for the open position
    PriceTicks mark{0};
    bool has_mark{false};
  };

  [[nodiscard]] static PriceTicks mark_of(const SymbolPosition& position) noexcept;
  static void apply_fill_to(SymbolPosition& position, std::int64_t& realised_pnl, Side side,
                            Quantity quantity, PriceTicks price_ticks);
  [[nodiscard]] PortfolioCheckResult evaluate(const std::map<SymbolId, SymbolPosition>& positions,
                                              std::int64_t realised_pnl) const;

  PortfolioRiskLimits limits_;
  std::map<SymbolId, SymbolPosition> positions_;
  std::int64_t realised_pnl_{0};
  RiskAuditTrail audit_;
  bool record_audit_{false};
};

} // namespace asterion
