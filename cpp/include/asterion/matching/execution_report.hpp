#pragma once

#include "asterion/core/types.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace asterion {

enum class OrderType : std::uint8_t { Limit = 1, Market = 2 };

enum class OrderStatus : std::uint8_t {
  New = 1,
  PartiallyFilled = 2,
  Filled = 3,
  Canceled = 4,
  Replaced = 5,
  Rejected = 6
};

enum class ExecType : std::uint8_t { New = 1, Trade = 2, Canceled = 3, Replaced = 4, Rejected = 5 };

enum class RejectReason : std::uint8_t {
  None = 0,
  InvalidQuantity = 1,
  InvalidPrice = 2,
  DuplicateClientOrderId = 3,
  UnknownOrder = 4,
  KillSwitch = 5,
  MaxOrderQuantity = 6,
  MaxNotional = 7,
  MaxPosition = 8,
  MaxGrossExposure = 9,
  PriceBand = 10,
  StaleMarketData = 11,
  Unsupported = 12,
  InternalError = 13,
  MaxOpenOrderQuantity = 14,
  MessageRateLimit = 15,
  SelfTradePrevention = 16
};

struct ExecutionReport {
  ClientOrderId client_order_id{kInvalidClientOrderId};
  OrderId exchange_order_id{kInvalidOrderId};
  SymbolId symbol_id{kInvalidSymbolId};
  Side side{Side::None};
  OrderStatus order_status{OrderStatus::Rejected};
  ExecType exec_type{ExecType::Rejected};
  Quantity filled_quantity{0};
  Quantity remaining_quantity{0};
  Quantity last_fill_quantity{0};
  PriceTicks last_fill_price_ticks{0};
  PriceTicks average_price_ticks{0};
  TimestampNs timestamp_ns{0};
  RejectReason reject_reason{RejectReason::None};
};

[[nodiscard]] std::string_view to_string(OrderType value) noexcept;
[[nodiscard]] std::string_view to_string(OrderStatus value) noexcept;
[[nodiscard]] std::string_view to_string(ExecType value) noexcept;
[[nodiscard]] std::string_view to_string(RejectReason value) noexcept;
[[nodiscard]] std::uint64_t append_to_checksum(std::uint64_t seed,
                                               const ExecutionReport& report) noexcept;
[[nodiscard]] std::uint64_t checksum_execution_reports(
    std::span<const ExecutionReport> reports) noexcept;

} // namespace asterion
