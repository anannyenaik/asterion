# Correctness

Asterion prioritizes correctness before optimization. The first implementation uses standard containers and explicit invariant checks so behavior is easy to reason about and regression-test.

## Order Book Invariants

The L3 book checks:

- resting orders must have positive quantity;
- order IDs must not appear in more than one FIFO queue;
- every order ID in a queue must be present in the lookup index;
- every lookup index entry must point to the correct side, price level and order;
- price-level aggregate quantity must equal the sum of child orders;
- empty price levels must be removed;
- best bid and best ask levels must not be empty.

## Golden Traces

Golden tests cover exact behavior:

- Scenario A: two sells at the same price are matched by a buy market order; FIFO priority is preserved and the second order remains partially filled.
- Scenario B: a buy order is replaced to a new price before a sell crosses; the trade is deterministic and the final book is empty.
- Scenario C: duplicate client order ID is rejected by risk.
- Scenario D: enabled kill switch rejects new orders.

## Replay Checksums

Replay validates sequence numbers and produces deterministic final book checksums. Execute and Trade activity also contributes to an activity checksum so replay behavior can be compared across runs.

## Property-Style Tests

The randomized tests generate add, cancel and replace streams, apply the same stream twice and assert:

- invariants hold after each operation;
- no order exists in two places;
- aggregate quantity equals child order quantity;
- final checksums match for identical input streams.

## Edge Cases Covered

- full and partial reductions;
- empty price-level removal;
- duplicate order IDs;
- duplicate client order IDs;
- stale market data;
- kill switch rejection;
- deterministic matching report order.

Additional fuzzing, malformed replay files and larger generated event sets are planned for Phase 2.
