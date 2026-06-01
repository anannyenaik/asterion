#include "asterion/market_data/spsc_replay.hpp"

#include "asterion/concurrency/spsc_ring_buffer.hpp"
#include "asterion/core/checksum.hpp"

#include <atomic>
#include <chrono>
#include <thread>

namespace asterion {

namespace {

// Fixed-size item carried through the SPSC queue. A single end_of_stream marker
// is published exactly once after the last event so the consumer can stop
// without any out-of-band signalling. MarketDataEvent is trivially copyable, so
// queue traffic never allocates after the ring buffer is constructed.
struct ReplayQueueItem {
  MarketDataEvent event{};
  bool end_of_stream{false};
};

void update_max_depth(std::atomic<std::size_t>& max_depth, std::size_t depth) {
  std::size_t prev = max_depth.load(std::memory_order_relaxed);
  while (depth > prev &&
         !max_depth.compare_exchange_weak(prev, depth, std::memory_order_relaxed)) {
  }
}

} // namespace

SpscReplayResult run_spsc_replay(std::span<const MarketDataEvent> events, SymbolId symbol_id,
                                 const SpscReplayConfig& config) {
  SpscReplayResult out;
  out.stats.queue_capacity = config.queue_capacity;
  out.stats.drop_policy_enabled = config.backpressure == SpscBackpressurePolicy::DropNewestOnFull;

  ReplayEngine engine(symbol_id, config.replay);
  engine.begin_stream(out.replay);
  // The event-log checksum is computed over the full input, identically to the
  // single-thread replay_events path, so it never depends on threading.
  out.replay.event_log_checksum = checksum_events(events);

  if (config.queue_capacity == 0) {
    out.replay.sequence_valid = false;
    out.replay.error = "spsc queue capacity must be at least 1";
    engine.finalize_stream(out.replay);
    return out;
  }

  SpscRingBuffer<ReplayQueueItem> queue(config.queue_capacity);

  // consumer_active lets the producer stop promptly (and avoid blocking forever
  // on a full queue) if the consumer halts early on a fatal diagnostic.
  std::atomic<bool> consumer_active{true};
  std::atomic<std::size_t> produced{0};
  std::atomic<std::size_t> backpressure{0};
  std::atomic<std::size_t> dropped{0};
  std::atomic<std::size_t> max_depth{0};
  std::atomic<std::size_t> eos_produced{0};

  const bool drop_on_full = config.backpressure == SpscBackpressurePolicy::DropNewestOnFull;

  const auto start = std::chrono::steady_clock::now();
  std::thread producer([&] {
    for (const MarketDataEvent& event : events) {
      if (!consumer_active.load(std::memory_order_acquire)) {
        return; // consumer halted early; stop producing to allow clean shutdown
      }
      const ReplayQueueItem item{event, false};
      bool pushed = queue.try_push(item);
      if (!pushed) {
        backpressure.fetch_add(1, std::memory_order_relaxed);
        if (drop_on_full) {
          dropped.fetch_add(1, std::memory_order_relaxed);
          continue; // opt-in overload shedding: discard this event
        }
        // Lossless blocking: yield until the consumer frees a slot or stops.
        while (!pushed) {
          if (!consumer_active.load(std::memory_order_acquire)) {
            return;
          }
          std::this_thread::yield();
          pushed = queue.try_push(item);
        }
      }
      produced.fetch_add(1, std::memory_order_relaxed);
      // size_approx is exact when read by the producer right after a push.
      update_max_depth(max_depth, queue.size_approx());
    }

    // Publish the end-of-stream marker exactly once. It is lossless even under
    // the drop policy: the marker must always be delivered for clean shutdown.
    ReplayQueueItem eos{};
    eos.end_of_stream = true;
    while (!queue.try_push(eos)) {
      if (!consumer_active.load(std::memory_order_acquire)) {
        return;
      }
      std::this_thread::yield();
    }
    eos_produced.store(1, std::memory_order_relaxed);
    update_max_depth(max_depth, queue.size_approx());
  });

  // Consumer runs on the calling thread.
  ReplayStreamState state;
  ReplayQueueItem item;
  for (;;) {
    if (!queue.try_pop(item)) {
      std::this_thread::yield();
      continue;
    }
    if (item.end_of_stream) {
      out.stats.end_of_stream_seen = true;
      out.stats.end_of_stream_markers_consumed = 1;
      break;
    }
    ++out.stats.consumed_events;
    if (!engine.replay_step(item.event, state, out.replay)) {
      // Fatal diagnostic: stop draining and signal the producer to wind down.
      consumer_active.store(false, std::memory_order_release);
      break;
    }
  }

  consumer_active.store(false, std::memory_order_release);
  producer.join();

  engine.finalize_stream(out.replay);
  const auto end = std::chrono::steady_clock::now();

  out.stats.produced_events = produced.load(std::memory_order_relaxed);
  out.stats.backpressure_count = backpressure.load(std::memory_order_relaxed);
  out.stats.dropped_events = dropped.load(std::memory_order_relaxed);
  out.stats.max_queue_depth = max_depth.load(std::memory_order_relaxed);
  out.stats.end_of_stream_markers_produced = eos_produced.load(std::memory_order_relaxed);
  out.stats.elapsed_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  out.stats.throughput_events_per_second =
      out.stats.elapsed_ns == 0
          ? 0.0
          : static_cast<double>(out.stats.consumed_events) * 1'000'000'000.0 /
                static_cast<double>(out.stats.elapsed_ns);
  return out;
}

SpscReplayResult run_spsc_replay_steady_state(std::span<const MarketDataEvent> events,
                                              SymbolId symbol_id,
                                              const SpscReplayConfig& config) {
  SpscReplayResult out;
  out.stats.queue_capacity = config.queue_capacity;
  out.stats.drop_policy_enabled = config.backpressure == SpscBackpressurePolicy::DropNewestOnFull;

  ReplayEngine engine(symbol_id, config.replay);
  engine.begin_stream(out.replay);
  out.replay.event_log_checksum = checksum_events(events);

  if (config.queue_capacity == 0) {
    out.replay.sequence_valid = false;
    out.replay.error = "spsc queue capacity must be at least 1";
    engine.finalize_stream(out.replay);
    return out;
  }

  SpscRingBuffer<ReplayQueueItem> queue(config.queue_capacity);
  std::atomic<bool> start{false};
  std::atomic<bool> consumer_active{true};
  std::atomic<std::size_t> ready_threads{0};
  std::atomic<std::size_t> produced{0};
  std::atomic<std::size_t> backpressure{0};
  std::atomic<std::size_t> dropped{0};
  std::atomic<std::size_t> max_depth{0};
  std::atomic<std::size_t> eos_produced{0};

  const bool drop_on_full = config.backpressure == SpscBackpressurePolicy::DropNewestOnFull;

  std::thread producer([&] {
    ready_threads.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    for (const MarketDataEvent& event : events) {
      if (!consumer_active.load(std::memory_order_acquire)) {
        return;
      }
      const ReplayQueueItem item{event, false};
      bool pushed = queue.try_push(item);
      if (!pushed) {
        backpressure.fetch_add(1, std::memory_order_relaxed);
        if (drop_on_full) {
          dropped.fetch_add(1, std::memory_order_relaxed);
          continue;
        }
        while (!pushed) {
          if (!consumer_active.load(std::memory_order_acquire)) {
            return;
          }
          std::this_thread::yield();
          pushed = queue.try_push(item);
        }
      }
      produced.fetch_add(1, std::memory_order_relaxed);
      update_max_depth(max_depth, queue.size_approx());
    }

    ReplayQueueItem eos{};
    eos.end_of_stream = true;
    while (!queue.try_push(eos)) {
      if (!consumer_active.load(std::memory_order_acquire)) {
        return;
      }
      std::this_thread::yield();
    }
    eos_produced.store(1, std::memory_order_relaxed);
    update_max_depth(max_depth, queue.size_approx());
  });

  std::thread consumer([&] {
    ready_threads.fetch_add(1, std::memory_order_release);
    while (!start.load(std::memory_order_acquire)) {
      std::this_thread::yield();
    }

    ReplayStreamState state;
    ReplayQueueItem item;
    for (;;) {
      if (!queue.try_pop(item)) {
        std::this_thread::yield();
        continue;
      }
      if (item.end_of_stream) {
        out.stats.end_of_stream_seen = true;
        out.stats.end_of_stream_markers_consumed = 1;
        break;
      }
      ++out.stats.consumed_events;
      if (!engine.replay_step(item.event, state, out.replay)) {
        consumer_active.store(false, std::memory_order_release);
        break;
      }
    }
    consumer_active.store(false, std::memory_order_release);
  });

  while (ready_threads.load(std::memory_order_acquire) < 2U) {
    std::this_thread::yield();
  }
  const auto measured_start = std::chrono::steady_clock::now();
  start.store(true, std::memory_order_release);

  producer.join();
  consumer.join();

  engine.finalize_stream(out.replay);
  const auto measured_end = std::chrono::steady_clock::now();

  out.stats.produced_events = produced.load(std::memory_order_relaxed);
  out.stats.backpressure_count = backpressure.load(std::memory_order_relaxed);
  out.stats.dropped_events = dropped.load(std::memory_order_relaxed);
  out.stats.max_queue_depth = max_depth.load(std::memory_order_relaxed);
  out.stats.end_of_stream_markers_produced = eos_produced.load(std::memory_order_relaxed);
  out.stats.elapsed_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(measured_end - measured_start).count());
  out.stats.throughput_events_per_second =
      out.stats.elapsed_ns == 0
          ? 0.0
          : static_cast<double>(out.stats.consumed_events) * 1'000'000'000.0 /
                static_cast<double>(out.stats.elapsed_ns);
  return out;
}

} // namespace asterion
