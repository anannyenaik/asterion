# Risk

Asterion implements a basic but real pre-trade risk gateway. It is intentionally separate from the matching engine so order validation is explicit and testable.

## Implemented Checks

- maximum order quantity;
- maximum notional value in ticks;
- maximum signed position per symbol;
- maximum gross exposure in ticks;
- price-band check against the latest reference price;
- duplicate client order ID rejection;
- global kill switch;
- stale market-data rejection;
- open-order (working) exposure per symbol (opt-in);
- per-client message-rate limiting in a fixed window (opt-in);
- self-trade prevention against a client's own resting orders (opt-in).

The three new controls default to a disabled sentinel in `RiskLimits` (`max_open_order_quantity` 0,
`max_messages_per_window` 0, `enable_self_trade_prevention` false), so existing configurations behave
exactly as before until they are explicitly enabled.

## Reject Reasons

Rejects are returned as structured enum values:

- `InvalidQuantity`
- `InvalidPrice`
- `DuplicateClientOrderId`
- `UnknownOrder`
- `KillSwitch`
- `MaxOrderQuantity`
- `MaxNotional`
- `MaxPosition`
- `MaxGrossExposure`
- `PriceBand`
- `StaleMarketData`
- `Unsupported`
- `InternalError`
- `MaxOpenOrderQuantity`
- `MessageRateLimit`
- `SelfTradePrevention`

## Kill Switch

When enabled, the kill switch rejects all new orders before any other risk check. It does not cancel existing orders in this first version; cancel-on-kill behavior is a future extension.

## Stale Data Policy

Each symbol has a latest reference price and market-data timestamp. New orders are rejected if no market state exists or if `now_ns - last_market_timestamp_ns` exceeds `stale_after_ns`.

## Working-Order Exposure

When `max_open_order_quantity > 0`, the gateway tracks the total resting (working) limit-order
quantity per symbol. An accepted limit order is registered as working; market orders are assumed to
take liquidity and do not contribute. A new order is rejected when the current working quantity plus
the incoming quantity would exceed the limit. Callers signal completion with
`RiskGateway::release_order(client_order_id)` once an order is fully filled or cancelled, which
decrements the working exposure. Working-order tracking is only active when this control or self-trade
prevention is enabled, so the default gateway does no extra work.

## Message-Rate Limiting

When `max_messages_per_window > 0` and `rate_window_ns > 0`, the gateway counts inbound orders per
`client_id` in a fixed window. The window resets when `now_ns - window_start >= rate_window_ns`. Each
client has an independent budget; a message that would exceed the budget is rejected with
`MessageRateLimit`. This is a fixed-window limiter (simple and allocation-free after warm-up), not a
sliding-window limiter.

## Self-Trade Prevention

When `enable_self_trade_prevention` is set, the gateway tracks each client's resting orders per symbol
and rejects a new order that would cross the same client's opposite side: a buy at or above the
client's best resting sell, or a sell at or below the client's best resting buy. Market orders from a
client that already has an opposite resting order are always rejected. Prevention scope is per-client;
crossing another client's order is matching's responsibility, not a self-trade.

## Audit Trail

Audit recording is opt-in. It is disabled by default because recording allocates (a per-entry
string and the trail vector) and the pre-trade reject path is intentionally allocation-free after
warm-up. Enable it with `RiskGateway::set_audit_enabled(true)` when an audit trail is wanted.

When enabled, every call to `check_new_order` appends a `RiskAuditEntry` to the gateway's
`RiskAuditTrail`, whether the order is accepted or rejected. Each entry records:

- timestamp (the `now_ns` passed to the check);
- client order ID, symbol and side;
- the decision (accepted or rejected) and reject reason;
- the deciding check name (for example `kill_switch`, `duplicate_client_order_id`,
  `stale_market_data`, `max_order_quantity`, `max_notional`, `max_position`, `message_rate_limit`,
  `self_trade_prevention`, `max_open_order_quantity` or `accepted`);
- the relevant configured limit value and the observed value that triggered the decision.

Audit entries depend only on the order flow and configured limits, not on wall-clock timing, so the
trail exposes a deterministic FNV-1a checksum (`RiskAuditTrail::checksum()`, equivalently
`checksum_risk_audit(entries)`). Identical order streams produce identical audit checksums, which
makes rejection behavior a reproducible artifact. Tests cover duplicate-ID, kill-switch, stale-data,
notional, quantity, position, working-order, message-rate and self-trade-prevention audit entries.

## Future Controls

Planned extensions include fat-finger bands by instrument class, cancel-on-disconnect,
cancel-on-kill, sliding-window rate limiting and persistent audit logs.
