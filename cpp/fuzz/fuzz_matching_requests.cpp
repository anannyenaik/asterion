#include "fuzz_support.hpp"

#include "asterion/matching/matching_engine.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

using namespace asterion;

namespace {

struct RunResult {
  std::uint64_t book_checksum{0};
  std::uint64_t reports_checksum{0};
};

[[nodiscard]] Quantity quantity_from_byte(std::uint8_t byte) {
  if (byte % 17U == 0U) {
    return 0;
  }
  if (byte % 19U == 0U) {
    return -static_cast<Quantity>((byte % 5U) + 1U);
  }
  return static_cast<Quantity>((byte % 20U) + 1U);
}

[[nodiscard]] PriceTicks price_from_byte(std::uint8_t byte) {
  if (byte % 23U == 0U) {
    return 0;
  }
  return 996 + static_cast<PriceTicks>(byte % 9U);
}

void require_reports_ok(const std::vector<ExecutionReport>& reports,
                        std::unordered_map<OrderId, ClientId>& owners, ClientId incoming_owner) {
  std::vector<const ExecutionReport*> trades;
  for (const ExecutionReport& report : reports) {
    fuzz::require(report.filled_quantity >= 0);
    fuzz::require(report.remaining_quantity >= 0);
    fuzz::require(report.last_fill_quantity >= 0);
    fuzz::require(report.last_fill_price_ticks >= 0);
    fuzz::require(report.average_price_ticks >= 0);
    fuzz::require(report.resting_price_ticks >= 0);
    if (report.exec_type == ExecType::New && report.exchange_order_id != kInvalidOrderId) {
      owners.emplace(report.exchange_order_id, incoming_owner);
    }
    if (report.exec_type == ExecType::Trade) {
      fuzz::require(report.last_fill_quantity > 0);
      fuzz::require(report.last_fill_price_ticks > 0);
      trades.push_back(&report);
    }
  }

  fuzz::require(trades.size() % 2U == 0U);
  for (std::size_t index = 0; index < trades.size(); index += 2U) {
    const ExecutionReport& resting = *trades[index];
    const ExecutionReport& incoming = *trades[index + 1U];
    fuzz::require(resting.last_fill_quantity == incoming.last_fill_quantity);
    fuzz::require(resting.last_fill_price_ticks == incoming.last_fill_price_ticks);
    const auto resting_owner = owners.find(resting.exchange_order_id);
    const auto incoming_order_owner = owners.find(incoming.exchange_order_id);
    fuzz::require(resting_owner != owners.end());
    fuzz::require(incoming_order_owner != owners.end());
    fuzz::require(resting_owner->second == 0U || incoming_order_owner->second == 0U ||
                  resting_owner->second != incoming_order_owner->second);
  }
}

void require_book_ok(const OrderBook& book) {
  fuzz::require(book.check_invariants().ok);
  const auto best_bid = book.best_bid();
  const auto best_ask = book.best_ask();
  fuzz::require(!best_bid || !best_ask || *best_bid < *best_ask);
}

[[nodiscard]] RunResult run_requests(std::span<const std::uint8_t> input) {
  MatchingEngine engine(1);
  std::unordered_map<OrderId, ClientId> owners;
  fuzz::ByteReader reader(input);
  const std::size_t operation_count =
      std::min(fuzz::kMaxMatchingOperations, std::max<std::size_t>(1U, input.size() / 8U));

  for (std::size_t index = 0; index < operation_count; ++index) {
    const std::uint8_t command = reader.next();
    const std::uint8_t side_byte = reader.next();
    const std::uint8_t price_byte = reader.next();
    const std::uint8_t quantity_byte = reader.next();
    const std::uint8_t client_order_byte = reader.next();
    const std::uint8_t exchange_order_byte = reader.next();
    const std::uint8_t owner_byte = reader.next();
    const std::uint8_t policy_byte = reader.next();

    const TimestampNs timestamp = static_cast<TimestampNs>(index + 1U);
    const ClientOrderId client_order_id = static_cast<ClientOrderId>(client_order_byte % 64U);
    const OrderId exchange_order_id = static_cast<OrderId>((exchange_order_byte % 64U) + 1U);
    const ClientId owner = static_cast<ClientId>(owner_byte % 5U);
    std::vector<ExecutionReport> reports;

    const bool is_cancel = command == static_cast<std::uint8_t>('C') || command % 11U == 0U;
    const bool is_replace = command == static_cast<std::uint8_t>('R') || command % 13U == 0U;
    if (is_cancel) {
      reports =
          engine.cancel_order(CancelOrderRequest{client_order_id, exchange_order_id, timestamp});
      require_reports_ok(reports, owners, 0);
    } else if (is_replace) {
      reports = engine.replace_order(
          ReplaceOrderRequest{client_order_id, exchange_order_id, price_from_byte(price_byte),
                              quantity_from_byte(quantity_byte), timestamp});
      require_reports_ok(reports, owners, 0);
    } else {
      NewOrderRequest request;
      request.client_order_id = client_order_id;
      request.symbol_id = policy_byte % 17U == 0U ? 2U : 1U;
      request.side = fuzz::side_from_byte(side_byte);
      request.order_type = OrderType::Limit;
      request.price_ticks = price_from_byte(price_byte);
      request.quantity = quantity_from_byte(quantity_byte);
      request.timestamp_ns = timestamp;
      request.client_id = owner;
      request.time_in_force = TimeInForce::Gtc;

      if (command == static_cast<std::uint8_t>('M') || command % 7U == 0U) {
        request.order_type = OrderType::Market;
        request.price_ticks = 0;
      }
      if (command == static_cast<std::uint8_t>('I') || policy_byte % 7U == 1U) {
        request.time_in_force = TimeInForce::Ioc;
      } else if (command == static_cast<std::uint8_t>('F') || policy_byte % 7U == 2U) {
        request.time_in_force = TimeInForce::Fok;
      }
      if (command == static_cast<std::uint8_t>('P') || policy_byte % 11U == 3U) {
        request.post_only = true;
      }

      reports = engine.submit_order(request);
      require_reports_ok(reports, owners, owner);
    }
    require_book_ok(engine.book());
  }

  return RunResult{engine.book().checksum(), engine.reports_checksum()};
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t* data, std::size_t size) {
  if (size == 0U || size > fuzz::kMaxMatchingInputSize) {
    return 0;
  }
  const std::span<const std::uint8_t> input(data, size);
  const RunResult first = run_requests(input);
  const RunResult second = run_requests(input);
  fuzz::require(first.book_checksum == second.book_checksum);
  fuzz::require(first.reports_checksum == second.reports_checksum);
  return 0;
}
