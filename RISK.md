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
- stale market-data rejection.

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

## Kill Switch

When enabled, the kill switch rejects all new orders before any other risk check. It does not cancel existing orders in this first version; cancel-on-kill behavior is a future extension.

## Stale Data Policy

Each symbol has a latest reference price and market-data timestamp. New orders are rejected if no market state exists or if `now_ns - last_market_timestamp_ns` exceeds `stale_after_ns`.

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
  `stale_market_data`, `max_order_quantity`, `max_notional`, `max_position` or `accepted`);
- the relevant configured limit value and the observed value that triggered the decision.

Audit entries depend only on the order flow and configured limits, not on wall-clock timing, so the
trail exposes a deterministic FNV-1a checksum (`RiskAuditTrail::checksum()`, equivalently
`checksum_risk_audit(entries)`). Identical order streams produce identical audit checksums, which
makes rejection behavior a reproducible artifact. Tests cover duplicate-ID, kill-switch, stale-data,
notional, quantity and position audit entries.

## Future Controls

Planned extensions include open-order exposure tracking, per-strategy limits, message-rate controls, fat-finger bands by instrument class, self-trade prevention, cancel-on-disconnect and persistent audit logs.
