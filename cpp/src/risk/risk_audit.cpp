#include "asterion/risk/risk_audit.hpp"

#include <utility>

namespace asterion {

std::uint64_t append_to_checksum(std::uint64_t seed, const RiskAuditEntry& entry) noexcept {
  seed = checksum_append(seed, entry.timestamp_ns);
  seed = checksum_append(seed, entry.client_order_id);
  seed = checksum_append(seed, entry.symbol_id);
  seed = checksum_append(seed, entry.side);
  seed = checksum_append(seed, static_cast<std::uint8_t>(entry.accepted ? 1U : 0U));
  seed = checksum_append(seed, entry.reject_reason);
  seed = checksum_append_string(seed, entry.check_name);
  seed = checksum_append(seed, entry.limit_value);
  seed = checksum_append(seed, entry.observed_value);
  return seed;
}

std::uint64_t checksum_risk_audit(std::span<const RiskAuditEntry> entries) noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  for (const RiskAuditEntry& entry : entries) {
    seed = append_to_checksum(seed, entry);
  }
  return seed;
}

void RiskAuditTrail::record(RiskAuditEntry entry) {
  checksum_ = append_to_checksum(checksum_, entry);
  if (entry.accepted) {
    ++accepted_count_;
  }
  entries_.push_back(std::move(entry));
}

void RiskAuditTrail::clear() noexcept {
  entries_.clear();
  checksum_ = kFnvOffsetBasis;
  accepted_count_ = 0;
}

} // namespace asterion
