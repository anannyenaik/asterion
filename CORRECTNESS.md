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

Replay validates contiguous sequence numbers and non-decreasing timestamps, then produces
deterministic final book checksums. Execute and Trade activity also contributes to an activity
checksum so replay behavior can be compared across runs. CSV and binary logs use the same
canonical event checksum, so equivalent files must replay to matching final-book,
execution-report and diagnostics checksums.

## Replay Diagnostics

Structured replay diagnostics report:

- sequence gaps;
- timestamp reversals;
- duplicate order IDs;
- unknown cancels, replaces and executes;
- invalid prices;
- invalid quantities, including over-reductions against a resting order;
- book invariant failures;
- crossed book states.

Each diagnostic carries event index, sequence number, symbol, severity and reason. The
diagnostics list is checksummed deterministically so failures can be compared across log formats.

## Aggregate Replay Views

Aggregate replay summaries are deterministic grouping helpers over the single-symbol replay engine.
They preserve the current replay implementation by partitioning events by symbol, replaying each
symbol independently, and reporting event counts, first/last sequence numbers, diagnostics and
checksum summaries per symbol. Per-symbol sequence validation is disabled by default because many
recorded multi-symbol feeds use one global sequence stream; it can be enabled explicitly to diagnose
symbol-local gaps.

## Inference Accounting

Feature extraction is versioned for the current top-of-book L2 feature vector:

- spread in ticks;
- midpoint in ticks;
- top-level imbalance;
- top-level quantity.

Measured inference records model scoring latency separately from replay and matching, then evaluates
timeout and late-signal policy hooks. Tests assert deterministic policy decisions and deterministic
linear-model scores; measured nanoseconds are treated as local timing observations, not portable
benchmark facts.

## Property-Style Tests

The randomized tests generate add, cancel, replace and crossing streams, apply deterministic seeds and assert:

- invariants hold after each operation;
- no order exists in two places;
- aggregate quantity equals child order quantity;
- final checksums match for identical input streams.
- generated replay corpora are deterministic for fixed seeds.

## Edge Cases Covered

- full and partial reductions;
- empty price-level removal;
- duplicate order IDs;
- duplicate client order IDs;
- zero quantity;
- invalid price;
- unknown cancel;
- unknown replace;
- sequence gap;
- timestamp reversal;
- malformed and truncated binary input;
- crossed book state;
- CSV-to-binary replay equivalence;
- aggregate per-symbol replay summaries;
- feature extraction and inference policy accounting;
- stale market data;
- kill switch rejection;
- deterministic matching report order.

Large fuzzing campaigns and exchange-specific malformed binary feeds remain out of scope for the current test suite.
