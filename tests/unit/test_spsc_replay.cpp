#include "asterion/core/checksum.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/market_data/spsc_replay.hpp"
#include "asterion/market_data/synthetic_generator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

using namespace asterion;

namespace {

std::filesystem::path samples_dir() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "samples";
}

std::vector<MarketDataEvent> synthetic(SyntheticFlowMode mode, std::size_t count) {
  SyntheticGeneratorConfig config;
  config.symbol_id = 1;
  config.event_count = count;
  config.mode = mode;
  config.seed = 12345;
  return generate_synthetic_events(config);
}

// Compares the single-thread replay path against the SPSC pipeline on the same
// events and asserts full deterministic parity plus lossless-by-default stats.
void require_spsc_parity(const std::vector<MarketDataEvent>& events, std::size_t queue_capacity) {
  REQUIRE_FALSE(events.empty());
  const SymbolId symbol_id = events.front().symbol_id;

  ReplayEngine baseline_engine(symbol_id);
  const ReplayResult baseline = baseline_engine.replay_events(events);

  SpscReplayConfig config;
  config.queue_capacity = queue_capacity;
  const SpscReplayResult spsc = run_spsc_replay(events, symbol_id, config);

  // Deterministic parity: every checksum and count must match the single-thread path.
  CHECK(spsc.replay.events_processed == baseline.events_processed);
  CHECK(spsc.replay.event_log_checksum == baseline.event_log_checksum);
  CHECK(spsc.replay.final_book_checksum == baseline.final_book_checksum);
  CHECK(spsc.replay.execution_report_checksum == baseline.execution_report_checksum);
  CHECK(spsc.replay.diagnostics_checksum == baseline.diagnostics_checksum);
  CHECK(spsc.replay.diagnostic_error_count == baseline.diagnostic_error_count);
  CHECK(spsc.replay.diagnostic_warning_count == baseline.diagnostic_warning_count);
  CHECK(spsc.replay.diagnostics.size() == baseline.diagnostics.size());
  CHECK(spsc.replay.sequence_valid == baseline.sequence_valid);

  // Lossless-by-default invariants.
  CHECK(spsc.stats.dropped_events == 0);
  CHECK_FALSE(spsc.stats.drop_policy_enabled);
  CHECK(spsc.stats.queue_capacity == queue_capacity);
  CHECK(spsc.stats.end_of_stream_markers_produced <= 1);
  CHECK(spsc.stats.end_of_stream_markers_consumed <= 1);
  // When the baseline did not halt early, the producer fed every event and the
  // consumer drained every event with a clean end-of-stream.
  if (baseline.sequence_valid) {
    CHECK(spsc.stats.end_of_stream_seen);
    CHECK(spsc.stats.end_of_stream_markers_produced == 1);
    CHECK(spsc.stats.end_of_stream_markers_consumed == 1);
    CHECK(spsc.stats.produced_events == events.size());
    CHECK(spsc.stats.consumed_events == events.size());
    CHECK(spsc.stats.produced_events == spsc.stats.consumed_events);
    CHECK(spsc.stats.elapsed_ns > 0);
    CHECK(spsc.stats.throughput_events_per_second > 0.0);
  }
}

void require_steady_state_spsc_parity(const std::vector<MarketDataEvent>& events,
                                      std::size_t queue_capacity) {
  REQUIRE_FALSE(events.empty());
  const SymbolId symbol_id = events.front().symbol_id;

  ReplayConfig replay_config;
  replay_config.validation_mode = ReplayValidationMode::Light;
  ReplayEngine baseline_engine(symbol_id, replay_config);
  const ReplayResult baseline = baseline_engine.replay_events(events);

  SpscReplayConfig config;
  config.queue_capacity = queue_capacity;
  config.replay = replay_config;
  const SpscReplayResult spsc = run_spsc_replay_steady_state(events, symbol_id, config);

  CHECK(spsc.replay.events_processed == baseline.events_processed);
  CHECK(spsc.replay.event_log_checksum == baseline.event_log_checksum);
  CHECK(spsc.replay.final_book_checksum == baseline.final_book_checksum);
  CHECK(spsc.replay.execution_report_checksum == baseline.execution_report_checksum);
  CHECK(spsc.replay.diagnostics_checksum == baseline.diagnostics_checksum);
  CHECK(spsc.replay.diagnostic_error_count == baseline.diagnostic_error_count);
  CHECK(spsc.replay.diagnostic_warning_count == baseline.diagnostic_warning_count);
  CHECK(spsc.replay.sequence_valid == baseline.sequence_valid);

  CHECK(spsc.stats.queue_capacity == queue_capacity);
  CHECK(spsc.stats.dropped_events == 0);
  CHECK_FALSE(spsc.stats.drop_policy_enabled);
  if (baseline.sequence_valid) {
    CHECK(spsc.stats.end_of_stream_seen);
    CHECK(spsc.stats.end_of_stream_markers_produced == 1);
    CHECK(spsc.stats.end_of_stream_markers_consumed == 1);
    CHECK(spsc.stats.produced_events == events.size());
    CHECK(spsc.stats.consumed_events == events.size());
    CHECK(spsc.stats.produced_events == spsc.stats.consumed_events);
    CHECK(spsc.stats.max_queue_depth <= queue_capacity);
    CHECK(spsc.stats.elapsed_ns > 0);
    CHECK(spsc.stats.throughput_events_per_second > 0.0);
  }
}

std::vector<MarketDataEvent> load_events(const std::filesystem::path& path) {
  EventLogReadResult log = read_event_log(path, EventLogFormat::Auto);
  REQUIRE(log.error.empty());
  return log.events;
}

} // namespace

TEST_CASE("spsc replay matches single-thread on checked-in sample", "[spsc][replay][parity]") {
  require_spsc_parity(load_events(samples_dir() / "sample_replay.csv"), 1024);
}

TEST_CASE("spsc replay matches single-thread on normalised binance fixture",
          "[spsc][replay][parity]") {
  require_spsc_parity(load_events(samples_dir() / "binance_depth_sample.normalised.bin"), 1024);
}

TEST_CASE("spsc replay matches single-thread on synthetic fixtures", "[spsc][replay][parity]") {
  require_spsc_parity(synthetic(SyntheticFlowMode::Balanced, 2000), 256);
  require_spsc_parity(synthetic(SyntheticFlowMode::HighCancellationRate, 2000), 256);
  require_spsc_parity(synthetic(SyntheticFlowMode::ReplaceHeavy, 2000), 256);
  require_spsc_parity(synthetic(SyntheticFlowMode::DeepBook, 2000), 256);
}

TEST_CASE("steady-state spsc replay matches single-thread on checked-in fixtures",
          "[spsc][replay][steady][parity]") {
  require_steady_state_spsc_parity(load_events(samples_dir() / "sample_replay.csv"), 16);
  require_steady_state_spsc_parity(load_events(samples_dir() / "binance_depth_sample.normalised.bin"),
                                   32);
}

TEST_CASE("steady-state spsc replay matches single-thread on generated fixtures",
          "[spsc][replay][steady][parity]") {
  require_steady_state_spsc_parity(synthetic(SyntheticFlowMode::Balanced, 2000), 64);
  require_steady_state_spsc_parity(synthetic(SyntheticFlowMode::HighCancellationRate, 2000), 64);
  require_steady_state_spsc_parity(synthetic(SyntheticFlowMode::ReplaceHeavy, 2000), 64);
  require_steady_state_spsc_parity(synthetic(SyntheticFlowMode::DeepBook, 2000), 64);
}

TEST_CASE("spsc replay is deterministic across repeated runs", "[spsc][replay][parity]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 3000);
  const SymbolId symbol_id = events.front().symbol_id;

  SpscReplayConfig config;
  config.queue_capacity = 64; // small enough to force backpressure
  const SpscReplayResult first = run_spsc_replay(events, symbol_id, config);
  for (int run = 0; run < 10; ++run) {
    const SpscReplayResult again = run_spsc_replay(events, symbol_id, config);
    CHECK(again.replay.final_book_checksum == first.replay.final_book_checksum);
    CHECK(again.replay.execution_report_checksum == first.replay.execution_report_checksum);
    CHECK(again.replay.diagnostics_checksum == first.replay.diagnostics_checksum);
    CHECK(again.replay.events_processed == first.replay.events_processed);
    CHECK(again.stats.dropped_events == 0);
    CHECK(again.stats.produced_events == again.stats.consumed_events);
  }
}

TEST_CASE("steady-state spsc replay is deterministic across repeated runs",
          "[spsc][replay][steady][parity]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::ReplaceHeavy, 3000);
  const SymbolId symbol_id = events.front().symbol_id;

  SpscReplayConfig config;
  config.queue_capacity = 64;
  config.replay.validation_mode = ReplayValidationMode::Light;
  const SpscReplayResult first = run_spsc_replay_steady_state(events, symbol_id, config);
  for (int run = 0; run < 10; ++run) {
    const SpscReplayResult again = run_spsc_replay_steady_state(events, symbol_id, config);
    CHECK(again.replay.final_book_checksum == first.replay.final_book_checksum);
    CHECK(again.replay.execution_report_checksum == first.replay.execution_report_checksum);
    CHECK(again.replay.diagnostics_checksum == first.replay.diagnostics_checksum);
    CHECK(again.replay.events_processed == first.replay.events_processed);
    CHECK(again.stats.dropped_events == 0);
    CHECK(again.stats.produced_events == again.stats.consumed_events);
  }
}

TEST_CASE("light validation mode preserves final replay checksums on small fixtures",
          "[replay][validation]") {
  for (const std::vector<MarketDataEvent>& events :
       {load_events(samples_dir() / "sample_replay.csv"),
        load_events(samples_dir() / "binance_depth_sample.normalised.bin"),
        synthetic(SyntheticFlowMode::Balanced, 500),
        synthetic(SyntheticFlowMode::HighCancellationRate, 500),
        synthetic(SyntheticFlowMode::ReplaceHeavy, 500),
        synthetic(SyntheticFlowMode::DeepBook, 500)}) {
    const SymbolId symbol_id = events.front().symbol_id;
    ReplayEngine full_engine(symbol_id);
    const ReplayResult full = full_engine.replay_events(events);

    ReplayConfig light_config;
    light_config.validation_mode = ReplayValidationMode::Light;
    ReplayEngine light_engine(symbol_id, light_config);
    const ReplayResult light = light_engine.replay_events(events);

    CHECK(light.events_processed == full.events_processed);
    CHECK(light.event_log_checksum == full.event_log_checksum);
    CHECK(light.final_book_checksum == full.final_book_checksum);
    CHECK(light.execution_report_checksum == full.execution_report_checksum);
    CHECK(light.diagnostics_checksum == full.diagnostics_checksum);
    CHECK(light.diagnostic_error_count == full.diagnostic_error_count);
    CHECK(light.diagnostic_warning_count == full.diagnostic_warning_count);
    CHECK(light.sequence_valid == full.sequence_valid);
  }
}

TEST_CASE("light validation keeps malformed input diagnostics deterministic",
          "[replay][validation]") {
  std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 1000);
  REQUIRE(events.size() > 50);
  events[50].sequence_number += 999;
  const SymbolId symbol_id = events.front().symbol_id;

  ReplayConfig light_config;
  light_config.validation_mode = ReplayValidationMode::Light;
  ReplayEngine first_engine(symbol_id, light_config);
  ReplayEngine second_engine(symbol_id, light_config);
  const ReplayResult first = first_engine.replay_events(events);
  const ReplayResult second = second_engine.replay_events(events);

  CHECK_FALSE(first.sequence_valid);
  CHECK(first.error == second.error);
  CHECK(first.events_processed == second.events_processed);
  CHECK(first.final_book_checksum == second.final_book_checksum);
  CHECK(first.diagnostics_checksum == second.diagnostics_checksum);
  CHECK(first.diagnostic_error_count == second.diagnostic_error_count);
}

TEST_CASE("unsupported replay validation mode fails clearly", "[replay][validation]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 10);
  ReplayConfig invalid_config;
  invalid_config.validation_mode = static_cast<ReplayValidationMode>(255);
  ReplayEngine engine(events.front().symbol_id, invalid_config);
  const ReplayResult result = engine.replay_events(events);

  CHECK_FALSE(result.sequence_valid);
  CHECK(result.error == "unsupported replay validation mode");
  REQUIRE_FALSE(result.diagnostics.empty());
  CHECK(result.diagnostics.front().reason == "unsupported replay validation mode");
}

TEST_CASE("spsc replay tiny queue forces backpressure but stays lossless",
          "[spsc][replay][backpressure]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 5000);
  const SymbolId symbol_id = events.front().symbol_id;

  // Capacity of 1 maximises producer blocking.
  SpscReplayConfig config;
  config.queue_capacity = 1;
  const SpscReplayResult spsc = run_spsc_replay(events, symbol_id, config);

  ReplayEngine baseline_engine(symbol_id);
  const ReplayResult baseline = baseline_engine.replay_events(events);

  CHECK(spsc.replay.final_book_checksum == baseline.final_book_checksum);
  CHECK(spsc.stats.dropped_events == 0);
  CHECK(spsc.stats.produced_events == spsc.stats.consumed_events);
  CHECK(spsc.stats.max_queue_depth <= config.queue_capacity);
  CHECK(spsc.stats.end_of_stream_seen);
  CHECK(spsc.stats.end_of_stream_markers_produced == 1);
  CHECK(spsc.stats.end_of_stream_markers_consumed == 1);
  // With capacity 1 and many events the producer must wait at least once.
  CHECK(spsc.stats.backpressure_count > 0);
}

TEST_CASE("steady-state spsc tiny queue forces backpressure but stays lossless",
          "[spsc][replay][steady][backpressure]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 5000);
  const SymbolId symbol_id = events.front().symbol_id;

  SpscReplayConfig config;
  config.queue_capacity = 1;
  config.replay.validation_mode = ReplayValidationMode::Light;
  const SpscReplayResult spsc = run_spsc_replay_steady_state(events, symbol_id, config);

  ReplayEngine baseline_engine(symbol_id, config.replay);
  const ReplayResult baseline = baseline_engine.replay_events(events);

  CHECK(spsc.replay.final_book_checksum == baseline.final_book_checksum);
  CHECK(spsc.stats.dropped_events == 0);
  CHECK(spsc.stats.produced_events == spsc.stats.consumed_events);
  CHECK(spsc.stats.max_queue_depth <= config.queue_capacity);
  CHECK(spsc.stats.end_of_stream_seen);
  CHECK(spsc.stats.end_of_stream_markers_produced == 1);
  CHECK(spsc.stats.end_of_stream_markers_consumed == 1);
  CHECK(spsc.stats.backpressure_count > 0);
}

TEST_CASE("spsc replay max queue depth never exceeds capacity", "[spsc][replay][backpressure]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 4000);
  const SymbolId symbol_id = events.front().symbol_id;
  for (std::size_t capacity : {2U, 8U, 64U, 512U}) {
    SpscReplayConfig config;
    config.queue_capacity = capacity;
    const SpscReplayResult spsc = run_spsc_replay(events, symbol_id, config);
    CHECK(spsc.stats.max_queue_depth <= capacity);
  }
}

TEST_CASE("steady-state spsc max queue depth never exceeds capacity",
          "[spsc][replay][steady][backpressure]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 4000);
  const SymbolId symbol_id = events.front().symbol_id;
  for (std::size_t capacity : {2U, 8U, 64U, 512U}) {
    SpscReplayConfig config;
    config.queue_capacity = capacity;
    config.replay.validation_mode = ReplayValidationMode::Light;
    const SpscReplayResult spsc = run_spsc_replay_steady_state(events, symbol_id, config);
    CHECK(spsc.stats.max_queue_depth <= capacity);
  }
}

TEST_CASE("spsc replay propagates consumer halt on malformed stream deterministically",
          "[spsc][replay][failure]") {
  // A sequence gap is a fatal diagnostic. Both paths must halt at the same event
  // with identical diagnostics, and the consumer must shut down cleanly (the
  // producer is always joined inside run_spsc_replay).
  std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 1000);
  REQUIRE(events.size() > 50);
  events[50].sequence_number += 999; // inject a sequence gap

  const SymbolId symbol_id = events.front().symbol_id;
  ReplayEngine baseline_engine(symbol_id);
  const ReplayResult baseline = baseline_engine.replay_events(events);
  REQUIRE_FALSE(baseline.sequence_valid);

  SpscReplayConfig config;
  config.queue_capacity = 8;
  const SpscReplayResult spsc = run_spsc_replay(events, symbol_id, config);

  CHECK_FALSE(spsc.replay.sequence_valid);
  CHECK(spsc.replay.events_processed == baseline.events_processed);
  CHECK(spsc.replay.diagnostics_checksum == baseline.diagnostics_checksum);
  CHECK(spsc.replay.final_book_checksum == baseline.final_book_checksum);
  CHECK(spsc.replay.error == baseline.error);
  // Consumer stopped before the end-of-stream marker on the fatal diagnostic.
  CHECK_FALSE(spsc.stats.end_of_stream_seen);
  CHECK(spsc.stats.dropped_events == 0);
}

TEST_CASE("steady-state spsc propagates consumer halt on malformed stream deterministically",
          "[spsc][replay][steady][failure]") {
  std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 1000);
  REQUIRE(events.size() > 50);
  events[50].sequence_number += 999;

  const SymbolId symbol_id = events.front().symbol_id;
  ReplayConfig replay_config;
  replay_config.validation_mode = ReplayValidationMode::Light;
  ReplayEngine baseline_engine(symbol_id, replay_config);
  const ReplayResult baseline = baseline_engine.replay_events(events);
  REQUIRE_FALSE(baseline.sequence_valid);

  SpscReplayConfig config;
  config.queue_capacity = 8;
  config.replay = replay_config;
  const SpscReplayResult spsc = run_spsc_replay_steady_state(events, symbol_id, config);

  CHECK_FALSE(spsc.replay.sequence_valid);
  CHECK(spsc.replay.events_processed == baseline.events_processed);
  CHECK(spsc.replay.diagnostics_checksum == baseline.diagnostics_checksum);
  CHECK(spsc.replay.final_book_checksum == baseline.final_book_checksum);
  CHECK(spsc.replay.error == baseline.error);
  CHECK_FALSE(spsc.stats.end_of_stream_seen);
  CHECK(spsc.stats.end_of_stream_markers_consumed == 0);
  CHECK(spsc.stats.dropped_events == 0);
}

TEST_CASE("spsc replay end-of-stream is delivered exactly once on clean stream",
          "[spsc][replay][failure]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 500);
  const SymbolId symbol_id = events.front().symbol_id;
  SpscReplayConfig config;
  config.queue_capacity = 32;
  const SpscReplayResult spsc = run_spsc_replay(events, symbol_id, config);
  CHECK(spsc.stats.end_of_stream_seen);
  CHECK(spsc.stats.end_of_stream_markers_produced == 1);
  CHECK(spsc.stats.end_of_stream_markers_consumed == 1);
  CHECK(spsc.stats.consumed_events == events.size());
}

TEST_CASE("steady-state spsc end-of-stream is delivered exactly once on clean stream",
          "[spsc][replay][steady][failure]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 500);
  const SymbolId symbol_id = events.front().symbol_id;
  SpscReplayConfig config;
  config.queue_capacity = 32;
  config.replay.validation_mode = ReplayValidationMode::Light;
  const SpscReplayResult spsc = run_spsc_replay_steady_state(events, symbol_id, config);
  CHECK(spsc.stats.end_of_stream_seen);
  CHECK(spsc.stats.end_of_stream_markers_produced == 1);
  CHECK(spsc.stats.end_of_stream_markers_consumed == 1);
  CHECK(spsc.stats.consumed_events == events.size());
}

TEST_CASE("spsc replay rejects zero queue capacity without deadlock", "[spsc][replay][failure]") {
  const std::vector<MarketDataEvent> events = synthetic(SyntheticFlowMode::Balanced, 10);
  SpscReplayConfig config;
  config.queue_capacity = 0;

  const SpscReplayResult per_replay = run_spsc_replay(events, events.front().symbol_id, config);
  const SpscReplayResult steady =
      run_spsc_replay_steady_state(events, events.front().symbol_id, config);

  CHECK_FALSE(per_replay.replay.sequence_valid);
  CHECK(per_replay.replay.error == "spsc queue capacity must be at least 1");
  CHECK_FALSE(steady.replay.sequence_valid);
  CHECK(steady.replay.error == "spsc queue capacity must be at least 1");
}

TEST_CASE("spsc replay opt-in drop policy sheds events under overload",
          "[spsc][replay][drop]") {
  // The drop policy is opt-in and intentionally lossy. Dropping order-lifecycle
  // events corrupts the book stream (a dropped Add makes a later Cancel an
  // "unknown order", a dropped event makes a sequence gap), so the drop policy is
  // NOT correctness-preserving for order-book replay and would deterministically
  // halt on the first such diagnostic. It is exercised here on a non-lifecycle
  // Heartbeat stream with order/sequence validation disabled, which is the only
  // regime where shedding is well defined. This documents the policy honestly:
  // produced + dropped accounts for every input event, and every event the
  // producer admitted is consumed in order.
  std::vector<MarketDataEvent> events;
  events.reserve(5000);
  for (std::size_t i = 0; i < 5000; ++i) {
    MarketDataEvent event{};
    event.timestamp_ns = static_cast<TimestampNs>(1'000'000 + i);
    event.sequence_number = static_cast<SequenceNumber>(i + 1);
    event.symbol_id = 1;
    event.event_type = MarketEventType::Heartbeat;
    events.push_back(event);
  }
  const SymbolId symbol_id = events.front().symbol_id;

  SpscReplayConfig config;
  config.queue_capacity = 1;
  config.backpressure = SpscBackpressurePolicy::DropNewestOnFull;
  config.replay.validate_sequence_numbers = false;
  config.replay.validate_timestamps = false;
  const SpscReplayResult spsc = run_spsc_replay(events, symbol_id, config);

  CHECK(spsc.stats.drop_policy_enabled);
  // The producer admitted every event it did not drop; nothing vanished silently.
  CHECK(spsc.stats.produced_events + spsc.stats.dropped_events == events.size());
  // Every admitted event was consumed in order, and the stream shut down cleanly.
  CHECK(spsc.stats.consumed_events == spsc.stats.produced_events);
  CHECK(spsc.replay.events_processed == spsc.stats.consumed_events);
  CHECK(spsc.stats.end_of_stream_seen);
}
