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

## Snapshot Loading

Snapshot events reset and reload the L3 book. A snapshot block is framed with begin/end flags in the
event `flags` field; the begin marker clears the book and each Snapshot record carrying a valid order
id reinstates one resting order. Reloaded orders use the snapshot record's timestamp and sequence
number so the resulting book checksum is deterministic. Tests assert that a snapshot reset produces
the same book checksum as an independently built book, that snapshot streams replay identically from
CSV and binary logs, that a payload-free begin marker clears the book, and that an invalid snapshot
order payload is rejected with a diagnostic.

## Aggregate Replay Views

Aggregate replay summaries are deterministic grouping helpers over the single-symbol replay engine.
They preserve the current replay implementation by partitioning events by symbol, replaying each
symbol independently, and reporting event counts, first/last sequence numbers, diagnostics and
checksum summaries per symbol. Per-symbol sequence validation is disabled by default because many
recorded multi-symbol feeds use one global sequence stream; it can be enabled explicitly to diagnose
symbol-local gaps. The opt-in shared replay path routes the same interleaved stream through
`MultiSymbolBookSet` and is tested for parity with grouped replay on deterministic generated
multi-symbol streams, fixed-seed fuzz streams, malformed multi-symbol diagnostics, snapshot bursts,
cancels and replaces, including combined book checksums and strict sequence diagnostics.
`compare_replay_parity(...)` exposes this as a structured report with per-symbol checksum agreement,
combined-book agreement and aggregate-checksum agreement. The report is a validation surface for the
opt-in shared path; grouped replay remains the default.

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

## Latency Budget Accounting

Latency-budget accounting is tested with injected, fixed durations rather than wall-clock
measurements, so the assertions are deterministic. Tests cover worst-case and total per stage,
budget utilization in parts-per-million, exceeded detection, worst-offender selection and a
configuration checksum that depends only on the configured budgets (never on observed timings).
Observed nanoseconds from `asterion_latency_budget` are treated as local timing observations, not
portable facts.

## Hot-Path Allocation And Checksums

The optimized benchmark path has deterministic tests for the non-timing properties that should not
depend on hardware: reusable L2 views match the value-returning view, the fixed-size imbalance
callback matches the vector-returning strategy API, a warmed non-creating target path has zero heap
allocations, reserved accepted risk checks do not allocate after warm-up and the hot-path checksum is
stable. Wall-clock latency values remain benchmark observations, not correctness assertions.

## Risk Audit Trail

When auditing is enabled, the risk gateway records every accepted and rejected decision in a
`RiskAuditTrail` (recording is opt-in so the default reject path stays allocation-free). Audit
entries depend only on the order flow and configured limits, so the trail's FNV-1a checksum is
deterministic and the incremental checksum matches a recomputation over the recorded entries. Tests
assert the audit entry fields (deciding check name, decision, reject reason, limit and observed
values) for duplicate-ID, kill-switch, stale-data, notional, quantity and position rejections.
Persistent JSONL audit logging is tested as append-only output with the cumulative deterministic
checksum in each entry. Rotated JSONL logs are verified by recomputing deterministic checksums
across every rotated file.

## Audit Manifest Signing

Audit manifests are tested over rotated logs for clean verification, serialization round-trips,
edited files, truncated files, missing files and reordered manifest entries. HMAC-SHA256 signing is
tested with a public fixture key, wrong-key rejection, missing-key rejection and signature-stripping
rejection. Signing covers deterministic manifest provenance and file entries; `created_at` remains
free-form provenance and is deliberately excluded from the signature payload.

## Opt-In Risk Controls

The open-order exposure, message-rate and self-trade-prevention controls are tested for accepts,
rejects and audit entries: working exposure rejects once projected resting quantity exceeds the cap,
updates from partial fills, full fills, cancels, rejects and replace reports, and still clears on
manual `release_order`; message-rate limiting throttles a client after its fixed-window or
sliding-window budget and resets/expires deterministically, with independent per-client budgets;
self-trade prevention rejects a client crossing its own resting order (including market orders) but
allows a different client. Cancel-on-kill tests assert tracked simulated working exposure is released
and later new orders are rejected. Cancel-on-disconnect tests assert tracked simulated working
exposure is released, new orders reject while disconnected under the default policy, and an explicit
allow policy is honored. Replace-risk tests cover accepted replacements, duplicate command IDs,
partial fills before replacement, working-exposure deltas, quantity/notional/price-band/position
rejects, self-trade rejects and audit checksums. A determinism test asserts the audit checksum is
reproducible and matches a recomputation, a compatibility test asserts the default gateway leaves all
three controls disabled, and an allocation test asserts the warm self-trade-prevention reject path
does not allocate.

## Inference Backend Selection

Backend selection is tested without the optional ONNX dependency: the Linear backend is selected and
scores deterministically; an ONNX request falls back to `LinearModel` (when ONNX Runtime is not
compiled in) with an honest detail string; and the selected backend integrates with feature
extraction and measured latency accounting, producing the same score as a reference `LinearModel`.
When ONNX Runtime is genuinely compiled in, a tiny checked-in identity fixture is decoded and loaded
to assert active backend selection, deterministic scoring and measured latency accounting.

## Simulated Session And Portfolio Risk

`SimulatedBrokerSession` tests cover accept/cancel lifecycles, disconnected acceptance rejection,
cancel rejection, fills, duplicate cancel requests, deterministic event checksums and
cancel-on-disconnect interaction with `RiskGateway` exposure release. The session is a deterministic
in-process state machine, not a live broker adapter.

`PortfolioRiskMonitor` tests cover disabled-by-default behavior, gross and net exposure rejects,
concentration rejects, simulated mark-to-market loss rejects, realised/unrealised PnL accounting,
position flips, deterministic audit entries and deterministic snapshot checksums. Marks are supplied
by the caller, so the tests validate simulated accounting behavior rather than live portfolio risk.

## Multi-Symbol Groundwork

`MultiSymbolBookSet` is tested for per-symbol routing of an interleaved stream (each book matches an
independently built single-symbol book), a deterministic combined checksum, rejecting an unknown
cancel without corrupting state, and parity between the shared replay path and the grouped replay
path for deterministic generated multi-symbol streams.

## Replay Output Stability

Replay checksums are asserted to be stable across repeated runs over the same input and to match
between CSV and binary encodings of the same events. These tests run against small checked-in sample
logs and skip automatically when the compiled Python bindings are not available.

## Benchmark Comparison Logic

The offline benchmark regression comparison is tested on synthetic in-memory and checked-in fixtures
(not measurements). Tests assert percentage-change computation, configurable threshold breaches,
new/missing benchmark detection, zero-baseline handling and JSON-serializable output. The historical
store and trend tooling is tested on synthetic fixtures: sequential default naming, explicit naming,
per-benchmark series with first/last/min/max/percentage change, benchmarks missing from some runs and
the empty-source error. These trends are tooling fixtures, not measurements.

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
- malformed CSV input;
- invalid binary headers, enum values and truncated binary input;
- invalid snapshot payloads;
- oversized CSV fields;
- crossed book state;
- CSV-to-binary replay equivalence;
- aggregate per-symbol replay summaries;
- shared multi-symbol replay parity with grouped replay;
- structured shared replay parity reports;
- snapshot reset, reload and CSV/binary snapshot replay equivalence;
- feature extraction and inference policy accounting;
- inference backend selection and ONNX fallback;
- multi-symbol single-pass routing groundwork;
- stale market data;
- working-order exposure, message-rate and self-trade-prevention controls;
- execution-report-driven risk exposure release;
- fixed-window and sliding-window rate limits;
- kill switch rejection and simulated cancel-on-kill exposure release;
- disconnect rejection and simulated cancel-on-disconnect exposure release;
- replace-order risk rechecks;
- persistent JSONL risk audit logging, rotation and verification;
- audit manifest generation, verification and optional HMAC signing;
- simulated broker/session lifecycle;
- simulated portfolio-risk accounting checks;
- deterministic matching report order.
- reusable L2 view correctness, fixed strategy callback equivalence and warmed hot-path allocation
  behavior.

Large fuzzing campaigns and exchange-specific malformed binary feeds remain out of scope for the current test suite.
