#pragma once

#include "asterion/core/checksum.hpp"
#include "asterion/core/types.hpp"
#include "asterion/matching/execution_report.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace asterion {

// A single pre-trade decision recorded for audit. The fields are deterministic given
// the same order flow, so a trail of entries produces a reproducible checksum.
struct RiskAuditEntry {
  TimestampNs timestamp_ns{0};
  ClientOrderId client_order_id{kInvalidClientOrderId};
  SymbolId symbol_id{kInvalidSymbolId};
  Side side{Side::None};
  bool accepted{false};
  RejectReason reject_reason{RejectReason::None};
  // Name of the deciding check, e.g. "kill_switch", "max_notional" or "accepted".
  std::string check_name;
  // Relevant configured limit and observed value for the deciding check (0 when not
  // applicable to that check).
  std::int64_t limit_value{0};
  std::int64_t observed_value{0};
};

[[nodiscard]] std::uint64_t append_to_checksum(std::uint64_t seed,
                                               const RiskAuditEntry& entry) noexcept;
[[nodiscard]] std::uint64_t checksum_risk_audit(std::span<const RiskAuditEntry> entries) noexcept;

class RiskAuditTrail {
public:
  void record(RiskAuditEntry entry);
  void clear() noexcept;

  [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
  [[nodiscard]] bool empty() const noexcept { return entries_.empty(); }
  [[nodiscard]] const std::vector<RiskAuditEntry>& entries() const noexcept { return entries_; }
  [[nodiscard]] std::uint64_t checksum() const noexcept { return checksum_; }
  [[nodiscard]] std::size_t accepted_count() const noexcept { return accepted_count_; }
  [[nodiscard]] std::size_t rejected_count() const noexcept {
    return entries_.size() - accepted_count_;
  }

private:
  std::vector<RiskAuditEntry> entries_;
  std::uint64_t checksum_{kFnvOffsetBasis};
  std::size_t accepted_count_{0};
};

} // namespace asterion
