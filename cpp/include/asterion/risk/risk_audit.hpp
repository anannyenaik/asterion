#pragma once

#include "asterion/core/checksum.hpp"
#include "asterion/core/types.hpp"
#include "asterion/matching/execution_report.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iosfwd>
#include <span>
#include <string>
#include <string_view>
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

enum class RiskAuditLogFormat : std::uint8_t { Text = 1, Jsonl = 2 };

struct RiskAuditVerificationResult {
  bool valid{true};
  std::size_t files_checked{0};
  std::size_t entries_checked{0};
  std::uint64_t final_checksum{kFnvOffsetBasis};
  std::string error;
};

[[nodiscard]] std::string_view to_string(RiskAuditLogFormat format) noexcept;
[[nodiscard]] std::uint64_t append_to_checksum(std::uint64_t seed,
                                               const RiskAuditEntry& entry) noexcept;
[[nodiscard]] std::uint64_t checksum_risk_audit(std::span<const RiskAuditEntry> entries) noexcept;
[[nodiscard]] std::string format_risk_audit_log_entry(
    const RiskAuditEntry& entry, std::uint64_t trail_checksum,
    RiskAuditLogFormat format = RiskAuditLogFormat::Jsonl);
void append_risk_audit_log_entry(std::ostream& output, const RiskAuditEntry& entry,
                                 std::uint64_t trail_checksum,
                                 RiskAuditLogFormat format = RiskAuditLogFormat::Jsonl);
[[nodiscard]] RiskAuditVerificationResult verify_risk_audit_logs(
    std::span<const std::filesystem::path> paths,
    RiskAuditLogFormat format = RiskAuditLogFormat::Jsonl);

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
