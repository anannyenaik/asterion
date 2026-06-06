# Shared replay parity

This document defines the grouped-vs-shared replay parity contract and records what
the parity tests cover and what they deliberately do not. It is scoped to Asterion's
deterministic replay lab and makes no claim about live trading, exchange connectivity,
order placement, cross-symbol matching, or production multi-venue replay.

## The two replay paths

Asterion replays a recorded, interleaved multi-symbol market-data stream in two ways.

- **Grouped replay** (`replay_by_symbol`, the default). The stream is partitioned by
  `symbol_id` and each symbol's events are replayed through an independent
  single-symbol `ReplayEngine`. This is the correctness-first path: it reuses the same
  validated, well-tested single-symbol engine that backs the rest of the lab, and it is
  the path the CLI and Python helpers use by default.
- **Shared replay** (`replay_shared_by_symbol`, opt-in). The interleaved stream is
  routed through a single `MultiSymbolBookSet` in one pass, keeping one `OrderBook` per
  symbol and dispatching each event to its symbol's book. It reproduces the grouped
  path's per-symbol validation and diagnostics inline so it can emit the same per-symbol
  summaries plus a deterministic combined-book checksum.

Both paths share the same `MarketDataEvent` schema, the same per-symbol validation
rules, and the same deterministic FNV-style checksums.

## Why both paths exist

Grouped replay is simple to reason about: each symbol is an isolated single-symbol
replay, so correctness follows directly from the single-symbol engine. Shared replay is
a single-pass routing surface that is useful when a caller wants to walk an interleaved
stream once rather than partitioning it first. Shared replay is **not** a cross-symbol
matching engine and does **not** introduce any cross-symbol semantics; it only routes
events to per-symbol books.

Grouped replay remains the default. Shared replay stays opt-in. This document and the
parity tests are evidence that the opt-in path agrees with the default on tested cases;
they are not, on their own, a reason to promote shared replay to the default.

## Order-id namespace

Order ids are **per-symbol**, not global. The same order id resting on two different
symbols is not a duplicate: each symbol's book has its own order index, and both paths
route by symbol before resolving an order id. A duplicate order id *within the same
symbol* is rejected as an error on both paths. The parity tests cover both cases
explicitly.

## The parity contract

For the tested deterministic cases, grouped and shared replay agree on:

- per-symbol final L2 book state (captured by `final_book_checksum`);
- per-symbol event counts and first/last sequence and timestamp;
- per-symbol `event_log_checksum`, `execution_report_checksum` and
  `diagnostics_checksum`;
- the combined book checksum (`combined_book_checksum`);
- the aggregate checksum (`aggregate_checksum`);
- per-symbol diagnostic warning/error counts and the `sequence_valid` flag;
- the ordered list of per-symbol replay diagnostics (severity, reason, indices);
- deterministic, repeated-run results (a second run of either path is bit-identical).

`compare_replay_parity(events, config)` returns a structured `ReplayParityReport` that
encodes exactly this: per-symbol checksum-match flags, combined-book and aggregate
checksum-match flags, the per-path symbol counts, and an overall `matched` flag that is
true only when every per-symbol checksum, the combined book checksum and the aggregate
checksum agree and both paths see the same symbols.

### Diagnostic index normalisation

Replay diagnostics carry an `event_index`. Both paths use a **per-symbol-local** event
index (the index of the event within that symbol's own stream), not a global stream
index. This is what makes the per-symbol `diagnostics_checksum` comparable between the
two paths: grouped replay sees only one symbol's events, while shared replay assigns the
index from the symbol's running event count. No other diagnostic field is normalised;
severities and reason strings are compared verbatim. If a future change makes a
diagnostic field differ by design, it must be normalised explicitly in
`compare_replay_parity` and documented here rather than silently ignored.

## Fixtures and test categories

C++ parity tests live in
[`tests/unit/test_shared_replay_parity.cpp`](../tests/unit/test_shared_replay_parity.cpp)
(alongside the original
[`tests/unit/test_multi_symbol.cpp`](../tests/unit/test_multi_symbol.cpp)). Python
parity tests live in
[`python/tests/test_shared_replay_parity.py`](../python/tests/test_shared_replay_parity.py)
and [`python/tests/test_replay_stability.py`](../python/tests/test_replay_stability.py).

Hand-written golden cases (compact, checked-in inline, no large corpora):

- two-symbol interleaved add/cancel/replace flow;
- three-symbol interleaving;
- multi-symbol snapshot begin/end flow;
- cancel after symbol interleaving;
- replace-heavy interleaving;
- sequence gap on one symbol while another continues (strict per-symbol sequences);
- timestamp reversal on one symbol;
- invalid event on one symbol;
- duplicate order id across different symbols (allowed by the namespace);
- duplicate order id within the same symbol (rejected on both paths);
- combined failure modes interleaved across symbols.

Fixed-seed random cases: a matrix of fixed seeds, symbol counts and flow modes
(`MultiSymbol`, `ReplaceHeavy`, `HighCancellationRate`) generated deterministically
in-test from `generate_synthetic_events`. No generated corpus is committed; the streams
are regenerated from the seed each run.

## Running the tests

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure          # includes the [parity] cases
python -m pytest python/tests/test_shared_replay_parity.py
```

To filter the C++ parity cases directly: `./build/asterion_tests "[parity]"`.

## Mismatch diagnostics

When parity fails, `describe_replay_parity(events, config)` returns a compact,
reproduction-focused report. It returns the literal string `replay parity matched`
when the paths agree; otherwise it lists, per affected symbol, the differing checksum
fields with both grouped and shared values, the first differing diagnostic (index,
sequence, severity and reason on each path), and—when the final book checksum
differs—the grouped path's expected top-of-book L2 levels. The C++ and Python parity
helpers attach this description to the failing assertion so a failure is reproducible
from the test log alone.

## What is covered, and what is not

Covered (for the tested deterministic cases): interleaved multi-symbol flows, snapshots,
cancels, replaces, executes, trades, heartbeats, per-symbol sequence gaps, timestamp
reversals, invalid events, both order-id duplicate cases, fixed-seed random corpora, and
deterministic repeat runs.

Not covered / explicitly out of scope:

- This is **not** an exhaustive proof of parity for all workloads. Parity coverage is
  stronger for tested cases; it is not exhaustively proven.
- No cross-symbol matching, cross-symbol sequencing, or any cross-symbol semantics.
- No live trading, authenticated exchange connectivity, broker connectivity, order
  placement, or production multi-venue replay.
- No production-HFT, portable-latency, profitability, alpha, or predictive-quality
  claims follow from this parity work.

## Current status and future work

- Grouped replay remains the correctness-first **default**.
- Shared replay remains **opt-in** (`replay_shared_by_symbol`, `--shared` in the
  inspection CLI, `aggregate_by_symbol(..., shared=True)` in Python).
- This parity evidence strengthens confidence in the opt-in path; it is not, by itself,
  a decision to change the default.

Possible future work: extend fixed-seed parity into a libFuzzer differential target,
widen the flow-mode and symbol-count matrix, and add per-level L2 reconstruction for the
shared path so the mismatch diagnostic can diff both books level-by-level rather than
showing only the grouped expected L2. None of this changes the default.
