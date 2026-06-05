# Matching And Order-State Semantics

Asterion implements deterministic exchange-style matching semantics for a local
research/systems lab. This document describes the in-process matching contract. It does not claim
real-exchange completeness, production exchange behaviour, live trading support, broker/exchange
connectivity or regulatory-grade order handling.

## Supported Requests

| Request | Supported semantics |
| --- | --- |
| Limit + `Gtc` | Match immediately at resting prices, then rest any remainder. |
| Limit + `Ioc` | Match immediately within the limit; cancel any unfilled remainder. Never rests. |
| Limit + `Fok` | Execute the full quantity immediately within the limit or reject before book mutation. |
| Market | Match at available resting prices; cancel any unfilled remainder. Market + `Fok` requires full immediate fillability. |
| Post-only limit + `Gtc` | Rest only when it would not immediately cross; otherwise reject before book mutation. |
| Cancel | Cancel a known resting order; reject an unknown or terminal order. |
| Replace | Cancel/reinsert the known resting order at the requested price and remaining quantity, so every successful replace loses FIFO priority. |

`post_only` is supported only with limit + `Gtc`. Other post-only combinations reject as
`Unsupported`. A market order with the default `Gtc` value retains the existing market-order
contract: it behaves as immediate-or-cancel because market orders never rest.

## Price-Time Priority

Incoming orders consume the best opposing price first. At one price, resting orders execute FIFO.
Trades use the resting order's price. FOK preflight sums only immediately executable prices, then a
successful FOK uses the same price-time matching path as other incoming orders.

## Order-State Transitions

The matching engine emits an execution report for each visible transition. `New` means accepted by
matching, including an order about to execute immediately; it is also the resting state for an
unfilled GTC limit because Asterion does not define a separate `Accepted`/`Resting` enum.

| Current state | Event | Next state | Report |
| --- | --- | --- | --- |
| none | valid request accepted | `New` | `ExecType::New` |
| `New` / `Replaced` | partial execution | `PartiallyFilled` | `Trade` |
| `New` / `PartiallyFilled` / `Replaced` | final execution | `Filled` | `Trade` |
| `New` / `PartiallyFilled` | IOC or market remainder | `Canceled` | `Canceled` |
| `New` / `PartiallyFilled` / `Replaced` | successful cancel | `Canceled` | `Canceled` |
| resting | successful replace | `Replaced` | `Replaced`, then optional trade reports |
| none / resting | invalid or policy-rejected command | existing state unchanged; command is `Rejected` | `Rejected` |

`Filled`, `Canceled` and `Rejected` are terminal for that order. A failed cancel or replace rejects
the command and does not change the target order. `Expired` and `PendingNew` are not implemented;
an IOC remainder uses `Canceled`, and a failed FOK uses `Rejected`.

## Execution-Report Examples

An IOC partial fill emits:

1. incoming `New`;
2. resting-order `Trade`;
3. incoming `Trade` with `PartiallyFilled`;
4. incoming `Canceled` with cumulative filled quantity retained and terminal remaining quantity `0`.

A failed FOK, crossing post-only order or matching STP backstop emits one `Rejected` report with no
exchange order ID and leaves the book unchanged. A successful FOK emits `New` followed by paired
resting/incoming `Trade` reports until the incoming order is `Filled`.

Successful cancel and replace reports identify the original target order; the cancel/replace
command's client order ID is used for duplicate-command detection. Unknown-command rejects identify
the command client order ID and have no exchange order ID.

## Reject Versus Cancel

Reject means the request or command was not accepted and must not mutate the book or target order.
The command still contributes to deterministic report and client-order-ID history. Cancel means an
accepted order or accepted remainder is deliberately terminated.

| Situation | Result | Reason / note |
| --- | --- | --- |
| Risk limit, kill switch or stale data | risk reject before matching | Structured `RejectReason`; no matching report unless the caller maps it into one. |
| Invalid new order | matching reject | `InvalidQuantity`, `InvalidPrice` or `Unsupported`. |
| Duplicate client order ID | matching/risk reject | `DuplicateClientOrderId`. Matching reserves IDs for accepted policy evaluation, including failed FOK/post-only/STP requests. |
| Unknown cancel or replace | matching/risk reject | `UnknownOrder`; target book state is unchanged. |
| Crossing post-only | matching reject | `PostOnlyWouldCross`; never trades or rests. |
| IOC unfilled remainder | cancel | `Canceled` with `RejectReason::None`; any fills remain in cumulative fields. |
| FOK not fully fillable | matching reject | `FokNotFillable`; no partial execution and unchanged book checksum. |
| Matching internal inconsistency | matching reject | `InternalError`; defensive path. |
| Successful cancel | cancel acknowledgement | `Canceled` report with `RejectReason::None`. |

## Self-Trade Prevention

Attributed orders have a non-zero `client_id`; `0` means unattributed. The optional risk-layer STP
is the preferred early gate. When enabled, it rejects an incoming normal limit, market, IOC or FOK
whose executable price range overlaps the same client's opposite resting order. This conservative
check occurs before matching and before any fill.

The matching engine applies the same incoming-order backstop for every non-zero `client_id`, even
when callers bypass the optional risk gate. It rejects the incoming order before mutation with
`SelfTradePrevention`. Different-owner orders match normally. Replace requests inherit the target
order's owner and reject without removing the target if the new price would self-cross.

Post-only cannot trade, so crossing post-only is classified as `PostOnlyWouldCross` rather than an
STP reject. The risk gateway allows that request to reach matching, then releases its temporary
working exposure when the matching reject report is consumed.

## Determinism And Limitations

- Client order IDs, exchange order IDs, price-time ordering, reports and checksums are deterministic.
- FOK failure, post-only crossing and matching STP rejection leave the book checksum unchanged.
- All successful replaces lose priority, including same-price quantity reductions.
- STP is one conservative reject-incoming policy; multiple venue-style STP policies are not modeled.
- There is no auction, hidden/iceberg quantity, pegging, stop order, expiry timer, trade bust,
  regulatory workflow or external venue session in the matching engine.
- The implementation is an in-process correctness and systems-research surface, not a real or
  production exchange.
