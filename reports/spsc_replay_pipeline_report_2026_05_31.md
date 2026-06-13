# Opt-In SPSC Replay Pipeline Evaluation (2026-05-31)

## Scope

This report documents **one** carefully scoped, opt-in concurrency boundary added to Asterion: a
deterministic bounded **single-producer/single-consumer (SPSC)** replay pipeline between market-data
replay and event processing.

Measurements are representative of the disclosed local environment. The
evaluation covers one bounded SPSC queue and excludes production networking,
live exchange or broker connectivity, broader lock-free architecture and
portable latency claims.

Deterministic single-thread replay (`ReplayEngine::replay_events`) **remains the default**. The SPSC
mode is strictly opt-in (`run_spsc_replay`) and exists to make a single concurrency boundary explicit,
testable and benchmarkable.

Follow-up note: the steady-state benchmark harness and opt-in light validation mode are documented in
[spsc_steady_state_report_2026_05_31.md](spsc_steady_state_report_2026_05_31.md). That follow-up keeps
the default single-thread replay path and full validation path unchanged, and frames the new work as
benchmark/evaluation only.

## Why a concurrency boundary was added

Asterion already had deterministic replay, schema guards, recorded-data tooling, optional ONNX
infrastructure, benchmark tooling, a pooled order book and zero-allocation feature extraction. The
remaining credibility gap was that it had **no explicit concurrency boundary** — every path was
single-threaded. The next systems question is how the project
handles a producer/consumer split, ownership across a thread boundary, backpressure and deterministic
parity under concurrency. This change answers that with the smallest defensible surface: one bounded
SPSC pipeline, proven to preserve the single-thread results exactly.

## Why SPSC (and not more)

- The replay pipeline has exactly one natural producer (the event source) and one natural consumer
  (the book/validation/diagnostics path). That is precisely the SPSC shape, so an SPSC queue is the
  simplest primitive that fits without inventing a more general (and harder to verify) MPMC queue.
- A single-producer/single-consumer ring buffer needs no read-modify-write atomics: each index is
  written by exactly one thread, so plain atomic stores with release/acquire pairing are sufficient
  and easy to reason about.
- Correctness and clarity were prioritised over cleverness. The consumer reuses the **exact**
  single-thread processing path, so parity is structural, not coincidental.

## Architecture

```text
preloaded events ──► producer thread ──► bounded SPSC ring buffer ──► consumer thread ──► ReplayEngine
  (std::span)          try_push in order      (fixed capacity)          try_pop in order     (book +
                       + end-of-stream                                                        validation +
                       marker (once)                                                          diagnostics +
                                                                                              checksums)
```

- **Producer** (`std::thread`): walks the preloaded events in order, publishes a fixed-size
  `ReplayQueueItem { MarketDataEvent event; bool end_of_stream; }` into the queue, preserves order,
  and emits exactly one end-of-stream marker after the last event.
- **Consumer** (calling thread): drains items in order and applies the same `ReplayEngine` streaming
  primitives (`begin_stream` / `replay_step` / `finalize_stream`) used by the single-thread path,
  computing the same book / execution-report / diagnostics checksums.
- The producer is always joined before `run_spsc_replay` returns (explicit, clean shutdown).

### Queue implementation

`cpp/include/asterion/concurrency/spsc_ring_buffer.hpp` is a header-only bounded ring buffer:

- single producer, single consumer only (documented; not a general MPMC queue);
- bounded capacity, fixed at construction; backing storage allocated exactly once, so steady-state
  `try_push`/`try_pop` never allocate;
- one reserved slot distinguishes full from empty without a separate counter;
- `head_` is owned by the consumer, `tail_` by the producer; each index is written by a single thread
  using release stores paired with acquire loads (memory-ordering rationale is commented in the
  header); the two indices sit on separate cache lines to avoid false sharing;
- unit-tested for push/pop/full/empty/FIFO order, wrap-around, and a two-thread transfer of 100k
  items (`tests/unit/test_spsc_ring_buffer.cpp`).

## Queue capacity and backpressure policy

- **Default policy: lossless blocking.** When the bounded queue is full the producer yields/spins
  (counted as backpressure) until the consumer frees a slot. No event is ever dropped. Under this
  policy `dropped_events == 0` and `produced_events == consumed_events` for any clean stream.
- **Opt-in policy: `DropNewestOnFull`.** For overload-shedding experiments only. It discards the
  current event when the queue is full and is **not correctness-preserving** for order-book streams:
  dropping market-data events creates sequence gaps and unknown-order references that deterministically
  halt replay. It is therefore only meaningful with order/sequence validation disabled, and it is
  documented as such. The end-of-stream marker is always delivered losslessly even under this policy.
- Default capacity is 1024 (`SpscReplayConfig::queue_capacity`, configurable; the benchmark exposes
  `--spsc-queue-capacity`).

## Stats reported

`SpscReplayStats`: `produced_events`, `consumed_events`, `queue_capacity`, `max_queue_depth`,
`backpressure_count`, `dropped_events` (zero unless the drop policy is enabled), `end_of_stream_seen`
and `drop_policy_enabled`. `max_queue_depth` and `backpressure_count` are timing-dependent and are
**never** part of any checksum.

## Deterministic parity results

The SPSC consumer reuses the single-thread `ReplayEngine` processing path on the same FIFO-ordered
events, so checksums are independent of thread scheduling. Parity is asserted in
`tests/unit/test_spsc_replay.cpp` and `python/tests/test_spsc_replay.py` across:

- the checked-in sample replay (`data/samples/sample_replay.csv`);
- the normalised Binance public-depth fixture (`data/samples/binance_depth_sample.normalised.bin`);
- synthetic **balanced**, **cancel-heavy** (`HighCancellationRate`), **replace-heavy** (`ReplaceHeavy`)
  and **deep-book** (`DeepBook`) fixtures.

For each, the SPSC path matches the single-thread path on: events processed, event-log checksum,
final book checksum, execution-report checksum, diagnostics checksum, diagnostic error/warning counts,
diagnostics size and `sequence_valid`. Under the default policy, `produced_events ==
consumed_events == event_count`, `dropped_events == 0` and end-of-stream is seen exactly once.
Repeated runs (10× in C++, 20× in Python, including with capacities small enough to force
backpressure) produce identical checksums.

## Failure modes tested

- **Tiny queue capacity (capacity 1)** over 5000 events: forces producer backpressure
  (`backpressure_count > 0`) while remaining lossless and bit-identical to the single-thread path;
  `max_queue_depth <= capacity` for capacities {1, 2, 8, 64, 512}.
- **Consumer halt / error propagation on malformed input:** an injected sequence gap halts both paths
  at the same event with identical diagnostics; the consumer signals the producer to wind down and the
  producer is still joined cleanly (no deadlock, no hang). `end_of_stream_seen` is correctly `false`
  in this halt case.
- **End-of-stream delivered exactly once** on a clean stream (`consumed_events == event_count`).
- **Opt-in drop policy** accounting: `produced_events + dropped_events == input size` and
  `consumed_events == produced_events`, exercised on a non-lifecycle heartbeat stream with validation
  disabled (the only regime where shedding is well defined).

Tests use deterministic fixtures, bounded work, explicit shutdown (`join`) and no sleeps/timeouts, so
they are not flaky.

## Allocation behaviour

The ring buffer allocates its backing storage exactly once at construction and never allocates during
`try_push`/`try_pop`. The dominant allocation in `run_spsc_replay` is **thread creation** (one
`std::thread` per call, ~tens of KB of stack), which is reported directly by the benchmark
(`bytes_allocated`). For a workload that creates the pipeline once and streams many events, this
per-call thread-lifecycle cost amortizes to near zero per event; for the per-run benchmark below it is
paid on every iteration and is the main reason the per-run SPSC latency is higher on a tiny dataset.

## Benchmark results (representative local measurements only)

Environment: Windows 10, MSYS2 UCRT64 GCC, `CMAKE_BUILD_TYPE=Release`. Dataset for the per-run rows is
the checked-in `data/samples/sample_hot_path_replay.bin` (12 events). `timing_mode = per-run`: each
latency sample is one whole-dataset replay run; each SPSC run spins up and joins a producer thread.

**These are representative local measurements on this machine/environment, not portable performance
claims.**

12-event sample, 2000 iterations:

| row | p50 (ns/run) | p95 | p99 | p99.9 | max | throughput (ev/s) | checksum parity |
|---|---|---|---|---|---|---|---|
| `replay_l3_diagnostics_single_thread` | 9,600 | 11,600 | 29,500 | 45,300 | 77,600 | ~1,083,091 | baseline |
| `spsc_replay_l3_diagnostics` | 172,500 | 256,200 | 335,200 | 749,700 | 927,400 | ~63,461 | **true** |

SPSC stats for that run: `queue_capacity=1024`, `produced_events=12`, `consumed_events=12`,
`backpressure_count=0`, `dropped_events=0`, `max_queue_depth=12`, `checksum_parity=true`.

Interpretation: on a 12-event dataset the per-run SPSC number is dominated by per-iteration thread
creation/join (~tens to ~hundreds of microseconds), not by per-event processing. This is the
cost of spinning the pipeline up and down 2000 times. SPSC is intended for long-running streams where
the producer thread is created once; the larger-corpus measurement below isolates the amortized
behaviour.

### Larger corpus (amortized thread cost, real backpressure)

To isolate the amortized behaviour and actually engage backpressure, the same two rows were run over a
generated 10,000-event balanced corpus (`build/spsc_demo_balanced.bin`, git-ignored), 25 iterations,
queue capacity 1024:

| row | throughput (ev/s) | backpressure_count | max_queue_depth | dropped | checksum parity |
|---|---|---|---|---|---|
| `replay_l3_diagnostics_single_thread` | ~2,089 | — | — | — | baseline |
| `spsc_replay_l3_diagnostics` | ~1,848 | 8,658 | 1,024 (saturated) | 0 | **true** |

Two constraints apply to these absolute numbers:

1. The balanced corpus grows the book without bound, and replay runs full book-state validation
   (`check_invariants()` + best-bid/ask) after **every** event. That validation is O(book size), so
   whole-corpus replay is roughly O(n²) and the absolute throughput here (~2k ev/s) is dominated by
   validation cost in **both** paths equally — it is not a statement about SPSC. The meaningful signal
   is the **ratio** and the backpressure behaviour, not the absolute number.
2. With per-event work this large, per-iteration thread creation is fully amortized: the SPSC pipeline
   runs at ~88% of single-thread throughput (~11% overhead), the bounded queue genuinely saturates
   (`max_queue_depth == capacity == 1024`), the producer waits 8,658 times under backpressure, nothing
   is dropped, and the result remains bit-identical (`checksum_parity == true`).

This is the representative "long-running stream" picture: a single producer thread, bounded blocking
backpressure that actually engages, and exact parity.

There are no machine-specific benchmark-number gates in CI; only checksum parity and stats invariants
are asserted by the test suite.

## Reproduction commands

```powershell
# Configure + build (Windows / MSYS2 UCRT64)
.\scripts\configure_release.ps1
$env:PATH = "C:\msys64\ucrt64\bin;$env:PATH"
cmake --build build

# C++ tests (includes SPSC queue + pipeline tests)
ctest --test-dir build --output-on-failure
.\build\asterion_tests.exe "[spsc]"

# Python tests + evaluation demo (needs the built bindings on PYTHONPATH)
$env:PYTHONPATH = "$PWD\build\python"
python -m pytest python/tests/test_spsc_replay.py
python scripts\run_spsc_replay_demo.py --input data\samples\sample_replay.csv --queue-capacity 4 --json

# Benchmark rows (single-thread baseline + SPSC pipeline)
.\build\asterion_benchmarks.exe --only-hot-path --hot-path-iterations 2000
.\build\asterion_benchmarks.exe --dataset build\spsc_demo_balanced.bin --only-hot-path `
    --hot-path-iterations 200 --spsc-queue-capacity 1024
```

Linux/macOS shell equivalents use `./scripts/configure_release.sh`, `PYTHONPATH=build/python` and the
same target/CLI names.

## Limitations

- Single-thread deterministic replay is the default; SPSC is opt-in.
- The pooled-book SPSC variant is not implemented because `ReplayEngine` is not templated on the book
  type. `PooledOrderBook` is covered by the existing single-thread hot-path benchmark rows.
- Per-run benchmark latency on a tiny dataset is dominated by thread lifecycle, not per-event cost.
- Larger-corpus throughput evaluation should use
  `spsc_replay_steady_state_l3_diagnostics` / `single_thread_replay_steady_state_l3_diagnostics`
  with an explicit validation mode, as documented in
  [spsc_steady_state_report_2026_05_31.md](spsc_steady_state_report_2026_05_31.md).
- The drop policy is opt-in, lossy and not correctness-preserving for order-book streams.
- Absolute numbers are local and non-portable; only checksum parity and stats invariants are stable.
