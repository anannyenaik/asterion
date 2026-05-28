#include "asterion/risk/risk_audit.hpp"

#include <ostream>
#include <utility>

namespace asterion {

namespace {

void write_json_string(std::ostream& output, std::string_view value) {
  output << '"';
  for (const char ch : value) {
    switch (ch) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      output << ch;
      break;
    }
  }
  output << '"';
}

} // namespace

std::string_view to_string(RiskAuditLogFormat format) noexcept {
  switch (format) {
  case RiskAuditLogFormat::Text:
    return "text";
  case RiskAuditLogFormat::Jsonl:
    return "jsonl";
  }
  return "unknown";
}

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

void append_risk_audit_log_entry(std::ostream& output, const RiskAuditEntry& entry,
                                 std::uint64_t trail_checksum, RiskAuditLogFormat format) {
  if (format == RiskAuditLogFormat::Text) {
    output << "timestamp_ns=" << entry.timestamp_ns
           << " client_order_id=" << entry.client_order_id << " symbol_id=" << entry.symbol_id
           << " side=" << to_string(entry.side)
           << " accepted=" << (entry.accepted ? "true" : "false")
           << " reject_reason=" << to_string(entry.reject_reason)
           << " check_name=" << entry.check_name << " limit_value=" << entry.limit_value
           << " observed_value=" << entry.observed_value << " checksum=" << trail_checksum
           << '\n';
    return;
  }

  output << "{\"timestamp_ns\":" << entry.timestamp_ns
         << ",\"client_order_id\":" << entry.client_order_id
         << ",\"symbol_id\":" << entry.symbol_id << ",\"side\":";
  write_json_string(output, to_string(entry.side));
  output << ",\"accepted\":" << (entry.accepted ? "true" : "false")
         << ",\"reject_reason\":";
  write_json_string(output, to_string(entry.reject_reason));
  output << ",\"check_name\":";
  write_json_string(output, entry.check_name);
  output << ",\"limit_value\":" << entry.limit_value
         << ",\"observed_value\":" << entry.observed_value
         << ",\"checksum\":" << trail_checksum << "}\n";
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
