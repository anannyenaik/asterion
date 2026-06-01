#pragma once

#include "asterion/market_data/event.hpp"
#include "asterion/market_data/replay.hpp"

#include <cstddef>
#include <span>

namespace asterion {

// Opt-in concurrent replay pipeline for systems evaluation.
//
// What this is:
//   A single deterministic concurrency boundary between market-data replay and
//   event processing. A producer thread publishes preloaded MarketDataEvent
//   records, in order, into a bounded single-producer/single-consumer queue. A
//   consumer thread drains them, in order, and applies the SAME ReplayEngine
//   processing path (book / validation / diagnostics) as the single-thread path,
//   producing a bit-identical ReplayResult.
//
// What this is NOT:
//   Not production networking, not live exchange/broker connectivity, not a
//   production-HFT architecture, and not a latency guarantee. Deterministic
//   single-thread replay (ReplayEngine::replay_events) remains the default; this
//   SPSC mode is strictly opt-in and exists to make the concurrency boundary
//   explicit, testable and benchmarkable.
//
// Backpressure policy:
//   The DEFAULT policy is lossless blocking. When the bounded queue is full the
//   producer spins/yields (counted as backpressure) until space is available, so
//   no event is ever dropped. A drop-on-full policy is available but OPT-IN
//   (drop_on_full == true); it deliberately discards events when the queue is
//   full and is intended only for overload-shedding experiments. Under the
//   default policy dropped_events is always zero and produced == consumed.

enum class SpscBackpressurePolicy : std::uint8_t {
  // Lossless: producer blocks (spins/yields) until the consumer frees a slot.
  // This is the default and the only policy that guarantees no event loss.
  Block = 0,
  // Opt-in overload shedding: producer drops the event when the queue is full.
  // Use only for explicit overload experiments; breaks produced == consumed.
  DropNewestOnFull = 1,
};

struct SpscReplayConfig {
  std::size_t queue_capacity{1024};
  SpscBackpressurePolicy backpressure{SpscBackpressurePolicy::Block};
  ReplayConfig replay{};
};

struct SpscReplayStats {
  std::size_t produced_events{0};
  std::size_t consumed_events{0};
  std::size_t queue_capacity{0};
  std::size_t max_queue_depth{0};
  // Number of times the producer found the queue full and had to wait (Block) or
  // shed (DropNewestOnFull). This is a timing-dependent backpressure signal and
  // is intentionally NOT part of any checksum.
  std::size_t backpressure_count{0};
  // Always zero under the default Block policy. Non-zero only under the opt-in
  // DropNewestOnFull policy.
  std::size_t dropped_events{0};
  // True when the consumer observed exactly one end-of-stream marker and stopped
  // on it (clean shutdown). False when the consumer halted early on a fatal
  // diagnostic before the marker arrived.
  bool end_of_stream_seen{false};
  bool drop_policy_enabled{false};
};

struct SpscReplayResult {
  // Identical in shape and (for the lossless path) in value to the single-thread
  // ReplayEngine::replay_events result on the same events.
  ReplayResult replay;
  SpscReplayStats stats;
};

// Runs the bounded SPSC replay pipeline over preloaded events. The producer runs
// on a spawned thread; the consumer runs on the calling thread. The producer is
// always joined before returning (clean, explicit shutdown). Checksums are
// independent of thread timing because FIFO order is preserved and the consumer
// applies events in exactly the single-thread order.
[[nodiscard]] SpscReplayResult run_spsc_replay(std::span<const MarketDataEvent> events,
                                               SymbolId symbol_id,
                                               const SpscReplayConfig& config = {});

} // namespace asterion
