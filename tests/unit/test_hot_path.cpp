#include "asterion/book/order_book.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event.hpp"
#include "asterion/risk/risk_gateway.hpp"
#include "asterion/strategy/imbalance_strategy.hpp"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using namespace asterion;

namespace {

Order make_order(OrderId order_id, Side side, PriceTicks price, Quantity quantity,
                 SequenceNumber sequence = 1) {
  return Order{order_id, order_id + 10'000, 1, side, price, quantity,
               static_cast<TimestampNs>(sequence), sequence};
}

bool apply_event(const MarketDataEvent& event, OrderBook& book,
                 std::uint64_t& activity_checksum) {
  switch (event.event_type) {
  case MarketEventType::Add:
    return book.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                event.side, event.price_ticks, event.quantity,
                                event.timestamp_ns, event.sequence_number});
  case MarketEventType::Cancel:
    return event.quantity > 0 ? book.reduce_order(event.order_id, event.quantity)
                              : book.cancel_order(event.order_id);
  case MarketEventType::Replace:
    return book.replace_order(event.order_id, event.price_ticks, event.quantity,
                              event.timestamp_ns, event.sequence_number);
  case MarketEventType::Execute:
    activity_checksum = append_to_checksum(activity_checksum, event);
    return book.reduce_order(event.order_id, event.quantity);
  case MarketEventType::Trade:
    activity_checksum = append_to_checksum(activity_checksum, event);
    return true;
  case MarketEventType::Snapshot:
    if ((event.flags & kSnapshotBeginFlag) != 0U) {
      book.clear();
    }
    if (event.order_id == kInvalidOrderId) {
      return true;
    }
    return book.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                event.side, event.price_ticks, event.quantity,
                                event.timestamp_ns, event.sequence_number});
  case MarketEventType::Heartbeat:
    return true;
  }
  return false;
}

std::vector<MarketDataEvent> hot_path_events() {
  return {
      MarketDataEvent{1, 1, 1, MarketEventType::Add, Side::Buy, 999, 300, 10, 0, 0},
      MarketDataEvent{2, 2, 1, MarketEventType::Add, Side::Sell, 1001, 100, 11, 0, 0},
      MarketDataEvent{3, 3, 1, MarketEventType::Execute, Side::Sell, 1001, 10, 11, 70, 0},
      MarketDataEvent{4, 4, 1, MarketEventType::Replace, Side::Buy, 998, 250, 10, 0, 0},
      MarketDataEvent{5, 5, 1, MarketEventType::Heartbeat, Side::None, 0, 0, 0, 0, 0},
  };
}

std::uint64_t run_hot_path_checksum() {
  const std::vector<MarketDataEvent> events = hot_path_events();
  OrderBook book(1);
  book.reserve_order_capacity(events.size());
  L2View view;
  view.reserve(2);
  ImbalanceStrategy strategy(0.50, 1);
  RiskGateway risk;
  risk.reserve_hot_path_capacity(events.size(), 1, 0);
  risk.on_market_data(1, 1000, 0);

  std::uint64_t activity_checksum = kFnvOffsetBasis;
  std::uint64_t guard = kFnvOffsetBasis;
  ClientOrderId next_client_order_id = 1;
  for (const MarketDataEvent& event : events) {
    const bool applied = apply_event(event, book, activity_checksum);
    book.fill_l2_view(2, view);
    risk.on_market_data(1, event.price_ticks > 0 ? event.price_ticks : 1000, event.timestamp_ns);
    const StrategyDecisionBatch decisions = strategy.on_l2_update_fixed(view);
    for (const StrategyDecision& decision : decisions) {
      const RiskResult result = risk.check_new_order(
          NewOrderRequest{next_client_order_id,
                          1,
                          decision.side,
                          decision.order_type,
                          decision.price_ticks,
                          decision.quantity,
                          event.timestamp_ns,
                          1},
          event.timestamp_ns);
      guard = checksum_append(guard, result.accepted ? 1U : 0U);
      guard = checksum_append(guard, result.reject_reason);
      ++next_client_order_id;
    }
    guard = checksum_append(guard, applied ? 1U : 0U);
    guard = checksum_append(guard, static_cast<std::uint64_t>(view.bids.size()));
    guard = checksum_append(guard, static_cast<std::uint64_t>(view.asks.size()));
  }

  guard = checksum_append(guard, activity_checksum);
  guard = checksum_append(guard, book.checksum());
  return guard;
}

} // namespace

TEST_CASE("Reusable L2 view matches value-returning L2 view", "[hot-path][book]") {
  OrderBook book(1);
  REQUIRE(book.add_order(make_order(1, Side::Buy, 1000, 30)));
  REQUIRE(book.add_order(make_order(2, Side::Buy, 999, 20)));
  REQUIRE(book.add_order(make_order(3, Side::Sell, 1001, 10)));
  REQUIRE(book.add_order(make_order(4, Side::Sell, 1002, 40)));

  const L2View value_view = book.l2_view(2);
  L2View reusable_view;
  reusable_view.reserve(2);
  book.fill_l2_view(2, reusable_view);

  REQUIRE(reusable_view.symbol_id == value_view.symbol_id);
  REQUIRE(reusable_view.bids.size() == value_view.bids.size());
  REQUIRE(reusable_view.asks.size() == value_view.asks.size());
  REQUIRE(reusable_view.bids[0].price_ticks == value_view.bids[0].price_ticks);
  REQUIRE(reusable_view.bids[0].quantity == value_view.bids[0].quantity);
  REQUIRE(reusable_view.asks[1].price_ticks == value_view.asks[1].price_ticks);
  REQUIRE(reusable_view.asks[1].quantity == value_view.asks[1].quantity);
}

TEST_CASE("Fixed imbalance callback is equivalent to vector callback",
          "[hot-path][strategy]") {
  OrderBook book(1);
  REQUIRE(book.add_order(make_order(1, Side::Buy, 1000, 100)));
  REQUIRE(book.add_order(make_order(2, Side::Sell, 1001, 10)));
  const L2View view = book.l2_view(1);

  ImbalanceStrategy strategy(0.50, 5);
  const std::vector<StrategyDecision> vector_decisions = strategy.on_l2_update(view);
  const StrategyDecisionBatch fixed_decisions = strategy.on_l2_update_fixed(view);

  REQUIRE(fixed_decisions.size == vector_decisions.size());
  REQUIRE(fixed_decisions.decisions[0].side == vector_decisions[0].side);
  REQUIRE(fixed_decisions.decisions[0].order_type == vector_decisions[0].order_type);
  REQUIRE(fixed_decisions.decisions[0].price_ticks == vector_decisions[0].price_ticks);
  REQUIRE(fixed_decisions.decisions[0].quantity == vector_decisions[0].quantity);
}

TEST_CASE("Target hot-path checksum is stable", "[hot-path][checksum]") {
  REQUIRE(run_hot_path_checksum() == run_hot_path_checksum());
  REQUIRE(run_hot_path_checksum() == 10361412861342163144ULL);
}
