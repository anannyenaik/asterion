#include "asterion/risk/risk_audit.hpp"

#include <charconv>
#include <fstream>
#include <optional>
#include <ostream>
#include <sstream>
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

template <typename T> [[nodiscard]] std::optional<T> parse_integral(std::string_view value) {
  T output{};
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(first, last, output);
  if (ec != std::errc() || ptr != last) {
    return std::nullopt;
  }
  return output;
}

[[nodiscard]] std::optional<std::string_view> json_value_for(std::string_view line,
                                                             std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t key_pos = line.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t begin = key_pos + needle.size();
  if (begin >= line.size()) {
    return std::nullopt;
  }
  if (line[begin] == '"') {
    ++begin;
    std::size_t end = begin;
    bool escaped = false;
    while (end < line.size()) {
      const char ch = line[end];
      if (!escaped && ch == '"') {
        return line.substr(begin, end - begin);
      }
      escaped = !escaped && ch == '\\';
      if (ch != '\\') {
        escaped = false;
      }
      ++end;
    }
    return std::nullopt;
  }

  std::size_t end = begin;
  while (end < line.size() && line[end] != ',' && line[end] != '}') {
    ++end;
  }
  return line.substr(begin, end - begin);
}

[[nodiscard]] std::optional<std::string_view> text_value_for(std::string_view line,
                                                             std::string_view key) {
  const std::string needle = std::string(key) + "=";
  std::size_t pos = 0;
  while (pos < line.size()) {
    const std::size_t next = line.find(' ', pos);
    const std::size_t end = next == std::string_view::npos ? line.size() : next;
    const std::string_view token = line.substr(pos, end - pos);
    if (token.starts_with(needle)) {
      return token.substr(needle.size());
    }
    if (next == std::string_view::npos) {
      break;
    }
    pos = next + 1U;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<Side> parse_side_token(std::string_view value) noexcept {
  if (value == "Buy") {
    return Side::Buy;
  }
  if (value == "Sell") {
    return Side::Sell;
  }
  if (value == "None") {
    return Side::None;
  }
  return std::nullopt;
}

[[nodiscard]] std::optional<RejectReason> parse_reject_reason_token(
    std::string_view value) noexcept {
  if (value == "None") {
    return RejectReason::None;
  }
  if (value == "InvalidQuantity") {
    return RejectReason::InvalidQuantity;
  }
  if (value == "InvalidPrice") {
    return RejectReason::InvalidPrice;
  }
  if (value == "DuplicateClientOrderId") {
    return RejectReason::DuplicateClientOrderId;
  }
  if (value == "UnknownOrder") {
    return RejectReason::UnknownOrder;
  }
  if (value == "KillSwitch") {
    return RejectReason::KillSwitch;
  }
  if (value == "MaxOrderQuantity") {
    return RejectReason::MaxOrderQuantity;
  }
  if (value == "MaxNotional") {
    return RejectReason::MaxNotional;
  }
  if (value == "MaxPosition") {
    return RejectReason::MaxPosition;
  }
  if (value == "MaxGrossExposure") {
    return RejectReason::MaxGrossExposure;
  }
  if (value == "PriceBand") {
    return RejectReason::PriceBand;
  }
  if (value == "StaleMarketData") {
    return RejectReason::StaleMarketData;
  }
  if (value == "Unsupported") {
    return RejectReason::Unsupported;
  }
  if (value == "InternalError") {
    return RejectReason::InternalError;
  }
  if (value == "MaxOpenOrderQuantity") {
    return RejectReason::MaxOpenOrderQuantity;
  }
  if (value == "MessageRateLimit") {
    return RejectReason::MessageRateLimit;
  }
  if (value == "SelfTradePrevention") {
    return RejectReason::SelfTradePrevention;
  }
  if (value == "Disconnected") {
    return RejectReason::Disconnected;
  }
  if (value == "MaxPortfolioGrossExposure") {
    return RejectReason::MaxPortfolioGrossExposure;
  }
  if (value == "MaxPortfolioNetExposure") {
    return RejectReason::MaxPortfolioNetExposure;
  }
  if (value == "MaxSymbolConcentration") {
    return RejectReason::MaxSymbolConcentration;
  }
  if (value == "MaxPortfolioLoss") {
    return RejectReason::MaxPortfolioLoss;
  }
  return std::nullopt;
}

[[nodiscard]] bool parse_bool_token(std::string_view value) noexcept {
  return value == "true";
}

template <typename T>
[[nodiscard]] bool assign_integral(std::optional<std::string_view> token, T& output) {
  if (!token) {
    return false;
  }
  const auto parsed = parse_integral<T>(*token);
  if (!parsed) {
    return false;
  }
  output = *parsed;
  return true;
}

[[nodiscard]] std::optional<RiskAuditEntry> parse_audit_entry_line(
    std::string_view line, RiskAuditLogFormat format, std::uint64_t& logged_checksum) {
  const auto value_for = [line, format](std::string_view key) {
    return format == RiskAuditLogFormat::Jsonl ? json_value_for(line, key)
                                               : text_value_for(line, key);
  };

  RiskAuditEntry entry;
  if (!assign_integral(value_for("timestamp_ns"), entry.timestamp_ns) ||
      !assign_integral(value_for("client_order_id"), entry.client_order_id) ||
      !assign_integral(value_for("symbol_id"), entry.symbol_id) ||
      !assign_integral(value_for("limit_value"), entry.limit_value) ||
      !assign_integral(value_for("observed_value"), entry.observed_value) ||
      !assign_integral(value_for("checksum"), logged_checksum)) {
    return std::nullopt;
  }

  const auto side_token = value_for("side");
  const auto reason_token = value_for("reject_reason");
  const auto side = side_token ? parse_side_token(*side_token) : std::optional<Side>{};
  const auto reason =
      reason_token ? parse_reject_reason_token(*reason_token) : std::optional<RejectReason>{};
  const auto accepted = value_for("accepted");
  const auto check_name = value_for("check_name");
  if (!side || !reason || !accepted || !check_name) {
    return std::nullopt;
  }
  entry.side = *side;
  entry.reject_reason = *reason;
  entry.accepted = parse_bool_token(*accepted);
  entry.check_name = std::string(*check_name);
  return entry;
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

std::string format_risk_audit_log_entry(const RiskAuditEntry& entry,
                                        std::uint64_t trail_checksum,
                                        RiskAuditLogFormat format) {
  std::ostringstream output;
  if (format == RiskAuditLogFormat::Text) {
    output << "timestamp_ns=" << entry.timestamp_ns
           << " client_order_id=" << entry.client_order_id << " symbol_id=" << entry.symbol_id
           << " side=" << to_string(entry.side)
           << " accepted=" << (entry.accepted ? "true" : "false")
           << " reject_reason=" << to_string(entry.reject_reason)
           << " check_name=" << entry.check_name << " limit_value=" << entry.limit_value
           << " observed_value=" << entry.observed_value << " checksum=" << trail_checksum
           << '\n';
    return output.str();
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
  return output.str();
}

void append_risk_audit_log_entry(std::ostream& output, const RiskAuditEntry& entry,
                                 std::uint64_t trail_checksum, RiskAuditLogFormat format) {
  output << format_risk_audit_log_entry(entry, trail_checksum, format);
}

RiskAuditVerificationResult verify_risk_audit_logs(std::span<const std::filesystem::path> paths,
                                                   RiskAuditLogFormat format) {
  RiskAuditVerificationResult result;
  std::uint64_t checksum = kFnvOffsetBasis;

  for (const std::filesystem::path& path : paths) {
    std::ifstream input(path);
    if (!input) {
      result.valid = false;
      result.error = "unable to open audit log: " + path.string();
      return result;
    }
    ++result.files_checked;

    std::string line;
    std::size_t line_number = 0;
    while (std::getline(input, line)) {
      ++line_number;
      if (line.empty()) {
        continue;
      }
      std::uint64_t logged_checksum = 0;
      auto entry = parse_audit_entry_line(line, format, logged_checksum);
      if (!entry) {
        result.valid = false;
        result.error = path.string() + ":" + std::to_string(line_number) +
                       ": malformed audit entry";
        return result;
      }
      checksum = append_to_checksum(checksum, *entry);
      ++result.entries_checked;
      if (checksum != logged_checksum) {
        result.valid = false;
        result.final_checksum = checksum;
        result.error = path.string() + ":" + std::to_string(line_number) +
                       ": checksum mismatch, expected " + std::to_string(checksum) +
                       ", received " + std::to_string(logged_checksum);
        return result;
      }
    }
  }

  result.final_checksum = checksum;
  return result;
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
