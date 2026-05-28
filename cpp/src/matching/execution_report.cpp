#include "asterion/matching/execution_report.hpp"

#include "asterion/core/checksum.hpp"

namespace asterion {

std::string_view to_string(OrderType value) noexcept {
  switch (value) {
  case OrderType::Limit:
    return "Limit";
  case OrderType::Market:
    return "Market";
  }
  return "Unknown";
}

std::string_view to_string(OrderStatus value) noexcept {
  switch (value) {
  case OrderStatus::New:
    return "New";
  case OrderStatus::PartiallyFilled:
    return "PartiallyFilled";
  case OrderStatus::Filled:
    return "Filled";
  case OrderStatus::Canceled:
    return "Canceled";
  case OrderStatus::Replaced:
    return "Replaced";
  case OrderStatus::Rejected:
    return "Rejected";
  }
  return "Unknown";
}

std::string_view to_string(ExecType value) noexcept {
  switch (value) {
  case ExecType::New:
    return "New";
  case ExecType::Trade:
    return "Trade";
  case ExecType::Canceled:
    return "Canceled";
  case ExecType::Replaced:
    return "Replaced";
  case ExecType::Rejected:
    return "Rejected";
  }
  return "Unknown";
}

std::string_view to_string(RejectReason value) noexcept {
  switch (value) {
  case RejectReason::None:
    return "None";
  case RejectReason::InvalidQuantity:
    return "InvalidQuantity";
  case RejectReason::InvalidPrice:
    return "InvalidPrice";
  case RejectReason::DuplicateClientOrderId:
    return "DuplicateClientOrderId";
  case RejectReason::UnknownOrder:
    return "UnknownOrder";
  case RejectReason::KillSwitch:
    return "KillSwitch";
  case RejectReason::MaxOrderQuantity:
    return "MaxOrderQuantity";
  case RejectReason::MaxNotional:
    return "MaxNotional";
  case RejectReason::MaxPosition:
    return "MaxPosition";
  case RejectReason::MaxGrossExposure:
    return "MaxGrossExposure";
  case RejectReason::PriceBand:
    return "PriceBand";
  case RejectReason::StaleMarketData:
    return "StaleMarketData";
  case RejectReason::Unsupported:
    return "Unsupported";
  case RejectReason::InternalError:
    return "InternalError";
  case RejectReason::MaxOpenOrderQuantity:
    return "MaxOpenOrderQuantity";
  case RejectReason::MessageRateLimit:
    return "MessageRateLimit";
  case RejectReason::SelfTradePrevention:
    return "SelfTradePrevention";
  }
  return "Unknown";
}

std::uint64_t append_to_checksum(std::uint64_t seed, const ExecutionReport& report) noexcept {
  seed = checksum_append(seed, report.client_order_id);
  seed = checksum_append(seed, report.exchange_order_id);
  seed = checksum_append(seed, report.symbol_id);
  seed = checksum_append(seed, report.side);
  seed = checksum_append(seed, report.order_status);
  seed = checksum_append(seed, report.exec_type);
  seed = checksum_append(seed, report.filled_quantity);
  seed = checksum_append(seed, report.remaining_quantity);
  seed = checksum_append(seed, report.last_fill_quantity);
  seed = checksum_append(seed, report.last_fill_price_ticks);
  seed = checksum_append(seed, report.average_price_ticks);
  seed = checksum_append(seed, report.resting_price_ticks);
  seed = checksum_append(seed, report.timestamp_ns);
  seed = checksum_append(seed, report.reject_reason);
  return seed;
}

std::uint64_t checksum_execution_reports(std::span<const ExecutionReport> reports) noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  for (const ExecutionReport& report : reports) {
    seed = append_to_checksum(seed, report);
  }
  return seed;
}

} // namespace asterion
