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

## Future Controls

Planned extensions include open-order exposure tracking, per-strategy limits, message-rate controls, fat-finger bands by instrument class, self-trade prevention, cancel-on-disconnect and persistent audit logs.
