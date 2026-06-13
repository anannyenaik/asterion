# Independent Reference Matcher (Test Oracle)

Asterion's matching contract is documented in
[matching_semantics.md](matching_semantics.md) and exercised by focused C++
unit, golden and property tests. This page describes an additional,
**independent** check on that contract: a small Python *reference matcher* that
re-implements the same documented semantics from scratch, plus a cross-check
harness that replays identical order flow into both the C++ engine and the
reference and compares the results.

This is specification-style reference testing for deterministic matching
semantics: a test oracle and second implementation of the documented contract.
Production-exchange completeness, regulatory validation and live connectivity
are outside scope.

## What it is

- [`python/asterion/testing/reference_matcher.py`](../python/asterion/testing/reference_matcher.py)
  — `ReferenceMatcher`, a deliberately simple re-implementation of Asterion's
  documented matching and order-state semantics using plain Python data
  structures (bid/ask books, FIFO queues per price, an order lookup by exchange
  id). It emits simplified deterministic execution reports with the same fields
  and field semantics as Asterion's `ExecutionReport`.
- [`python/asterion/testing/cross_check.py`](../python/asterion/testing/cross_check.py)
  — request builders, a deterministic random order-flow generator, and
  `run_cross_check(...)`, which replays one operation stream into both matchers
  and compares their canonical outputs.

The reference matcher is reached through Asterion's existing Python bindings'
**data types only** (`NewOrderRequest`, `CancelOrderRequest`,
`ReplaceOrderRequest`, the `Side`/`OrderType`/`TimeInForce`/`OrderStatus`/
`ExecType`/`RejectReason` enums and `ExecutionReport`). It does **not** call the
C++ matching implementation internally — it is a genuinely independent second
description of the contract.

## Why it exists

A single implementation can be self-consistent and still wrong. An independent
re-derivation of the same documented semantics turns "the engine agrees with
itself" into "two independent implementations agree", and makes any divergence
reproducible from a seed. It complements — does not replace — the C++ unit,
golden and property suites.

## Semantics covered

The reference models the documented Asterion contract:

- GTC limit (match then rest remainder), market (immediate-or-cancel), IOC
  (match then cancel remainder, never rests), FOK (full-fill preflight or
  reject before any book mutation);
- post-only limit + GTC (rest when non-crossing, otherwise `PostOnlyWouldCross`
  before any trade);
- cancel (known resting order succeeds; unknown/terminal rejects with
  `UnknownOrder`);
- replace (cancel/reinsert at the new price and remaining quantity, so every
  successful replace **loses FIFO priority**; may re-cross and trade);
- partial fill, full fill, successful cancel;
- invalid quantity/price, duplicate client order id, unsupported combinations;
- the matching-layer self-trade-prevention reject-incoming backstop for
  attributed (`client_id != 0`) flow, including the replace self-cross case;
- price-time priority: best opposing price first, FIFO within a price, trades at
  the resting order's price;
- the validation/reservation ordering of the engine (e.g. a client order id is
  reserved for accepted policy evaluation, including failed FOK / post-only /
  STP requests, but not for the early invalid-quantity / invalid-price
  rejects).

The engine's matching is risk-independent: the reference models the matching
engine alone, with no `RiskGateway`, mirroring `MatchingEngine` in C++.

## What is compared

For each replayed stream, `run_cross_check` compares:

1. **The full execution-report sequence**, field by field
   (`client_order_id`, `exchange_order_id`, `symbol_id`, `side`,
   `order_status`, `exec_type`, `filled_quantity`, `remaining_quantity`,
   `last_fill_quantity`, `last_fill_price_ticks`, `average_price_ticks`,
   `resting_price_ticks`, `timestamp_ns`, `reject_reason`). Enum fields are
   compared by name so the same value compares equal regardless of which side
   produced it.
2. **The final L2 book** — price and aggregate quantity per level, both sides,
   from `MatchingEngine.book().l2_view(...)` versus the reference's L2.
3. **The C++ canonical report checksum** — `MatchingEngine.reports_checksum()`
   recomputed over the reference's reports via the bound
   `checksum_execution_reports`, so both checksums are produced by the *same*
   hashing function over each side's report stream.

## Comparison Scope

- **L3 FIFO of the resting book is not directly enumerated.** The Python
  bindings expose the C++ book as an aggregate L2 view and per-order lookup, not
  a full per-order FIFO walk. FIFO/price-time ordering is therefore validated
  *indirectly but precisely* through the trade-report sequence (which resting
  `exchange_order_id` fills first, and in what order), together with the final
  aggregate L2. The golden `test_replace_loses_priority` case pins this
  behaviour explicitly.
- The reference's own `reports_checksum()` is a stable FNV-1a digest of its
  canonical state for replay-stability assertions; it is intentionally *not*
  required to equal any C++ book checksum. Cross-engine agreement uses the
  shared `checksum_execution_reports` hashing described above.
- This is a single conservative STP policy and the documented matching contract
  only; it models no auctions, hidden/iceberg quantity, pegging, stop orders,
  expiry, trade busts or venue sessions — exactly as
  [matching_semantics.md](matching_semantics.md) states for the engine itself.

## How to run

Build the bindings and run the tests on a single interpreter (see the Windows
toolchain note in [evidence_index.md](evidence_index.md)):

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DASTERION_BUILD_PYTHON=ON
cmake --build build
PYTHONPATH=build/python python -m pytest \
  python/tests/test_reference_matcher_golden.py \
  python/tests/test_reference_matcher_property.py
```

- [`python/tests/test_reference_matcher_golden.py`](../python/tests/test_reference_matcher_golden.py)
  — hand-written flows (GTC rest, market/partial/IOC/FOK fills, post-only,
  cancel, replace-loses-priority, replace self-cross, STP, duplicate/unknown
  ids), each asserting the two matchers agree *and* pinning the expected
  outcome.
- [`python/tests/test_reference_matcher_property.py`](../python/tests/test_reference_matcher_property.py)
  — 20 fixed-seed random streams (120 ops each) plus deterministic-replay
  checks.

## Reproducing a failing seed

The property tests are deterministic. On failure, the assertion message already
contains the seed, the first differing report (with its op index and a
human-readable description of the operation), any final-L2 difference, any
checksum difference, and the full operation sequence. To re-run a single seed
interactively:

```python
from asterion.testing.cross_check import generate_stream, run_cross_check

ops = generate_stream(seed=20260601, n_ops=120)
result = run_cross_check(ops)
print(result.matched)
print(result.detail)          # first divergence + full op stream
for op in ops:
    print(op.describe())       # exact, replayable operation list
```

Because `generate_stream(seed, n_ops)` is pure for a given seed, the same seed
always reproduces the same stream, and the printed `op.describe()` lines are a
concrete, minimisable repro you can trim down to the smallest failing prefix.

## Future Differential Fuzzing

The native [`fuzz_matching_requests`](../FUZZING.md) target currently checks
bounded request streams against matching-engine invariants and repeated native
execution. It deliberately does not embed Python in libFuzzer. A future
differential-fuzzing bridge could translate minimized native fuzz inputs into
the existing `cross_check` operation format, then compare them against this
reference matcher as a separate reproducible step.
