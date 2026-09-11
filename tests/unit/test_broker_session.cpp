#include "asterion/core/checksum.hpp"
#include "asterion/risk/risk_gateway.hpp"
#include "asterion/session/broker_session.hpp"

#include <catch2/catch_test_macros.hpp>

using namespace asterion;

namespace {

NewOrderRequest order(ClientOrderId id, Side side, PriceTicks price, Quantity quantity,
                      TimestampNs now_ns) {
  return NewOrderRequest{id, 1, side, OrderType::Limit, price, quantity, now_ns, 7};
}

} // namespace

TEST_CASE("Broker session models a full accept/cancel lifecycle", "[session]") {
  SimulatedBrokerSession session;
  session.connect(1);
  REQUIRE(session.connected());

  REQUIRE(session.on_order_accepted(10'001, 1, 1, Side::Buy, 100, 2));
  REQUIRE(session.live_order_count() == 1);
  REQUIRE(session.order_state(10'001) == BrokerOrderState::Accepted);

  REQUIRE(session.request_cancel(10'001, 3));
  REQUIRE(session.order_state(10'001) == BrokerOrderState::PendingCancel);
  REQUIRE(session.pending_cancel_count() == 1);

  REQUIRE(session.acknowledge_cancel(10'001, 4));
  REQUIRE(session.order_state(10'001) == BrokerOrderState::Canceled);
  REQUIRE(session.live_order_count() == 0);

  const BrokerSessionSnapshot snapshot = session.snapshot();
  REQUIRE(snapshot.connect_count == 1);
  REQUIRE(snapshot.canceled_order_count == 1);
  REQUIRE(snapshot.event_count == 4);
  REQUIRE(snapshot.event_checksum == checksum_broker_session_events(session.events()));
  REQUIRE(snapshot.event_checksum != kFnvOffsetBasis);
}

TEST_CASE("Broker session rejects accepting orders while disconnected", "[session]") {
  SimulatedBrokerSession session;
  REQUIRE_FALSE(session.on_order_accepted(1, 1, 1, Side::Buy, 10, 1));
  session.connect(2);
  REQUIRE(session.on_order_accepted(1, 1, 1, Side::Buy, 10, 3));
}

TEST_CASE("Broker session handles cancel rejection and order fill", "[session]") {
  SimulatedBrokerSession session;
  session.connect(1);
  REQUIRE(session.on_order_accepted(7, 1, 1, Side::Buy, 10, 2));

  REQUIRE(session.request_cancel(7, 3));
  REQUIRE(session.reject_cancel(7, 4));
  REQUIRE(session.order_state(7) == BrokerOrderState::Accepted);
  REQUIRE(session.snapshot().cancel_reject_count == 1);

  // A fill can still arrive after a rejected cancel.
  REQUIRE(session.on_order_filled(7, 5));
  REQUIRE(session.order_state(7) == BrokerOrderState::Filled);
  REQUIRE(session.snapshot().filled_order_count == 1);
  REQUIRE(session.live_order_count() == 0);

  // Terminal orders reject further transitions.
  REQUIRE_FALSE(session.request_cancel(7, 6));
  REQUIRE_FALSE(session.on_order_filled(7, 7));
}

TEST_CASE("Broker session ignores duplicate cancel requests", "[session]") {
  SimulatedBrokerSession session;
  session.connect(1);
  REQUIRE(session.on_order_accepted(7, 1, 1, Side::Buy, 10, 2));

  REQUIRE(session.request_cancel(7, 3));
  const std::size_t events_before = session.events().size();
  REQUIRE_FALSE(session.request_cancel(7, 4));
  REQUIRE(session.events().size() == events_before);
  REQUIRE(session.pending_cancel_count() == 1);
}

TEST_CASE("Broker session event checksum is deterministic across identical runs", "[session]") {
  const auto run = []() {
    SimulatedBrokerSession session;
    session.connect(1);
    (void)session.on_order_accepted(10'001, 1, 1, Side::Buy, 100, 2);
    (void)session.on_order_accepted(10'002, 2, 1, Side::Sell, 80, 3);
    (void)session.request_cancel(10'001, 4);
    (void)session.acknowledge_cancel(10'001, 5);
    (void)session.on_order_filled(10'002, 6);
    return session.event_checksum();
  };
  REQUIRE(run() == run());
}

TEST_CASE("Cancel-on-disconnect flows through the broker session into the risk gateway",
          "[session][risk][disconnect]") {
  RiskLimits limits;
  limits.max_open_order_quantity = 500;
  limits.cancel_on_disconnect = true;
  RiskGateway risk(limits);
  risk.set_audit_enabled(true);
  risk.on_market_data(1, 1000, 0);

  REQUIRE(risk.check_new_order(order(1, Side::Buy, 1000, 100, 10), 10).accepted);
  REQUIRE(risk.check_new_order(order(2, Side::Sell, 1001, 80, 11), 11).accepted);
  REQUIRE(risk.working_quantity(1) == 180);

  SimulatedBrokerSession session(true);
  session.attach_risk_gateway(&risk);
  session.connect(5);
  REQUIRE(session.on_order_accepted(10'001, 1, 1, Side::Buy, 100, 6));
  REQUIRE(session.on_order_accepted(10'002, 2, 1, Side::Sell, 80, 7));
  REQUIRE(session.live_order_count() == 2);

  session.disconnect(20);
  REQUIRE_FALSE(session.connected());
  REQUIRE(session.pending_cancel_count() == 2);

  // The gateway released its tracked simulated working exposure and recorded audit
  // entries for the disconnect cancels.
  REQUIRE_FALSE(risk.connected());
  REQUIRE(risk.working_quantity(1) == 0);
  const RiskExposureSnapshot exposure = risk.exposure_snapshot();
  REQUIRE(exposure.disconnect_count == 1);
  REQUIRE(exposure.disconnect_cancel_count == 2);
  REQUIRE(exposure.working_order_count == 0);

  session.reconnect(30);
  REQUIRE(session.connected());
  REQUIRE(risk.connected());
  REQUIRE(risk.check_new_order(order(3, Side::Buy, 1000, 10, 31), 31).accepted);
}
