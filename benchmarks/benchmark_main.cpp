#include "asterion/book/order_book.hpp"
#include "asterion/book/pooled_order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/core/checksum.hpp"
#include "asterion/inference/backend.hpp"
#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/risk_gateway.hpp"
#include "asterion/strategy/imbalance_strategy.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#if defined(__linux__) || defined(__APPLE__)
#include <sys/utsname.h>
#endif

#ifndef ASTERION_GIT_COMMIT
#define ASTERION_GIT_COMMIT "unknown"
#endif

#ifndef ASTERION_BUILD_TYPE
#define ASTERION_BUILD_TYPE "unknown"
#endif

#ifndef ASTERION_COMPILER_FLAGS
#define ASTERION_COMPILER_FLAGS ""
#endif

using namespace asterion;

namespace {

struct Options {
  std::filesystem::path dataset_path{std::filesystem::path(ASTERION_SOURCE_DIR) / "data" /
                                     "samples" / "sample_hot_path_replay.bin"};
  std::optional<std::filesystem::path> json_path;
  std::size_t hot_path_iterations{5'000};
  std::size_t warmup_iterations{5};
  bool text_output{true};
  bool only_hot_path{false};
  std::string logging_mode;
};

struct LatencyDistribution {
  bool available{false};
  std::uint64_t p50_ns{0};
  std::uint64_t p95_ns{0};
  std::uint64_t p99_ns{0};
  std::uint64_t p999_ns{0};
  std::uint64_t max_ns{0};
};

struct BenchmarkResult {
  std::string name;
  std::size_t iterations{0};
  std::size_t warmup_iterations{0};
  std::size_t measured_iterations{0};
  std::size_t event_count{0};
  std::size_t risk_check_count{0};
  std::string dataset_name;
  std::string timing_mode{"aggregate"};
  // "core" benchmarks measure replay/book/matching/risk paths. "inference"
  // benchmarks measure feature extraction and model scoring; they are reported
  // separately so inference timings are never conflated with the trading hot path.
  std::string category{"core"};
  // Inference-only metadata. Empty for core benchmarks.
  std::string backend;
  std::string model_name;
  std::string input_shape;
  std::size_t feature_count{0};
  std::uint32_t feature_version{0};
  std::uint64_t total_ns{0};
  std::uint64_t avg_ns{0};
  double throughput_events_per_second{0.0};
  std::uint64_t guard{0};
  AllocationSnapshot allocations;
  LatencyDistribution latency;
};

Order make_order(OrderId order_id, Side side, PriceTicks price, Quantity quantity) {
  return Order{order_id, order_id + 1'000'000, 1, side, price, quantity,
               static_cast<TimestampNs>(order_id), order_id};
}

void print_usage(std::ostream& output) {
  output << "Usage: asterion_benchmarks [dataset.csv] [--dataset path] [--json path]"
         << " [--no-text] [--logging-mode name] [--hot-path-iterations n]"
         << " [--warmup-iterations n] [--hot-path-warmup n] [--only-hot-path]\n";
}

bool parse_size_option(std::string_view name, std::string_view value, std::size_t& output) {
  const std::string token(value);
  if (token.empty() || token.front() == '-') {
    std::cerr << name << " requires a non-negative integer\n";
    return false;
  }
  char* end = nullptr;
  const unsigned long long parsed = std::strtoull(token.c_str(), &end, 10);
  if (end == nullptr || *end != '\0') {
    std::cerr << name << " requires a non-negative integer\n";
    return false;
  }
  output = static_cast<std::size_t>(parsed);
  return true;
}

bool parse_options(int argc, char** argv, Options& options) {
  bool logging_mode_overridden = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg(argv[i]);
    if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      return false;
    }
    if (arg == "--dataset") {
      if (i + 1 >= argc) {
        std::cerr << "--dataset requires a path\n";
        return false;
      }
      options.dataset_path = argv[++i];
      continue;
    }
    if (arg == "--json") {
      if (i + 1 >= argc) {
        std::cerr << "--json requires a path\n";
        return false;
      }
      options.json_path = std::filesystem::path(argv[++i]);
      continue;
    }
    if (arg == "--no-text") {
      options.text_output = false;
      continue;
    }
    if (arg == "--only-hot-path") {
      options.only_hot_path = true;
      continue;
    }
    if (arg == "--logging-mode") {
      if (i + 1 >= argc) {
        std::cerr << "--logging-mode requires a value\n";
        return false;
      }
      options.logging_mode = argv[++i];
      logging_mode_overridden = true;
      continue;
    }
    if (arg == "--hot-path-iterations") {
      if (i + 1 >= argc) {
        std::cerr << "--hot-path-iterations requires a value\n";
        return false;
      }
      if (!parse_size_option(arg, argv[++i], options.hot_path_iterations)) {
        return false;
      }
      continue;
    }
    if (arg == "--warmup-iterations" || arg == "--hot-path-warmup") {
      if (i + 1 >= argc) {
        std::cerr << arg << " requires a value\n";
        return false;
      }
      if (!parse_size_option(arg, argv[++i], options.warmup_iterations)) {
        return false;
      }
      continue;
    }
    if (!arg.empty() && arg.front() == '-') {
      std::cerr << "unknown option: " << arg << '\n';
      return false;
    }
    options.dataset_path = std::filesystem::path(argv[i]);
  }

  if (!logging_mode_overridden) {
    if (options.json_path.has_value() && options.text_output) {
      options.logging_mode = "stdout+json";
    } else if (options.json_path.has_value()) {
      options.logging_mode = "json";
    } else {
      options.logging_mode = "stdout";
    }
  }
  return true;
}

template <typename Fn>
BenchmarkResult run_benchmark(std::string name, std::size_t iterations, Fn&& fn) {
  reset_allocation_counters();
  const auto start = std::chrono::steady_clock::now();
  const std::uint64_t guard = fn();
  const auto end = std::chrono::steady_clock::now();
  const AllocationSnapshot allocations = allocation_snapshot();
  const auto total_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  const double throughput = total_ns == 0
                                ? 0.0
                                : static_cast<double>(iterations) * 1'000'000'000.0 /
                                      static_cast<double>(total_ns);
  BenchmarkResult result;
  result.name = std::move(name);
  result.iterations = iterations;
  result.measured_iterations = iterations;
  result.event_count = iterations;
  result.total_ns = total_ns;
  result.avg_ns = iterations == 0 ? 0 : total_ns / iterations;
  result.throughput_events_per_second = throughput;
  result.guard = guard;
  result.allocations = allocations;
  return result;
}

std::size_t nearest_rank_index(std::size_t sample_count, std::uint32_t permille) {
  if (sample_count == 0) {
    return 0;
  }
  const std::size_t rank =
      (static_cast<std::size_t>(permille) * sample_count + 999U) / 1000U;
  return rank == 0 ? 0 : rank - 1U;
}

LatencyDistribution latency_distribution(std::vector<std::uint64_t> samples) {
  LatencyDistribution distribution;
  if (samples.empty()) {
    return distribution;
  }
  std::sort(samples.begin(), samples.end());
  distribution.available = true;
  distribution.p50_ns = samples[nearest_rank_index(samples.size(), 500)];
  distribution.p95_ns = samples[nearest_rank_index(samples.size(), 950)];
  distribution.p99_ns = samples[nearest_rank_index(samples.size(), 990)];
  distribution.p999_ns = samples[nearest_rank_index(samples.size(), 999)];
  distribution.max_ns = samples.back();
  return distribution;
}

// Per-call sampled runner used by the inference benchmarks so they report a real
// p50/p95/p99/p99.9/max distribution rather than only an aggregate average. The
// sample buffer is reserved before the allocation counters are reset, so push_back
// does not perturb the steady-state allocation count. fn receives the pre-sized
// buffer and must record exactly one latency sample per iteration.
template <typename Fn>
BenchmarkResult run_sampled_benchmark(std::string name, std::size_t iterations, Fn&& fn) {
  std::vector<std::uint64_t> samples;
  samples.reserve(iterations);

  reset_allocation_counters();
  const auto start = std::chrono::steady_clock::now();
  const std::uint64_t guard = fn(samples);
  const auto end = std::chrono::steady_clock::now();
  const AllocationSnapshot allocations = allocation_snapshot();

  const auto total_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  const double throughput = total_ns == 0
                                ? 0.0
                                : static_cast<double>(iterations) * 1'000'000'000.0 /
                                      static_cast<double>(total_ns);
  BenchmarkResult result;
  result.name = std::move(name);
  result.iterations = iterations;
  result.measured_iterations = iterations;
  result.event_count = iterations;
  result.timing_mode = "per-call";
  result.total_ns = total_ns;
  result.avg_ns = iterations == 0 ? 0 : total_ns / iterations;
  result.throughput_events_per_second = throughput;
  result.guard = guard;
  result.allocations = allocations;
  result.latency = latency_distribution(std::move(samples));
  return result;
}

template <typename Book>
bool apply_hot_path_event(const MarketDataEvent& event, Book& book,
                          std::uint64_t& activity_checksum) {
  switch (event.event_type) {
  case MarketEventType::Add:
    return book.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                event.side, event.price_ticks, event.quantity,
                                event.timestamp_ns, event.sequence_number});
  case MarketEventType::Cancel:
    return event.quantity > 0 ? book.reduce_order(event.order_id, event.quantity)
                              : book.cancel_order(event.order_id);
  case MarketEventType::Replace:
    return book.replace_order(event.order_id, event.price_ticks, event.quantity,
                              event.timestamp_ns, event.sequence_number);
  case MarketEventType::Execute:
    activity_checksum = append_to_checksum(activity_checksum, event);
    return book.reduce_order(event.order_id, event.quantity);
  case MarketEventType::Trade:
    activity_checksum = append_to_checksum(activity_checksum, event);
    return true;
  case MarketEventType::Snapshot:
    if ((event.flags & kSnapshotBeginFlag) != 0U) {
      book.clear();
    }
    if (event.order_id == kInvalidOrderId) {
      return true;
    }
    return book.add_order(Order{event.order_id, kInvalidClientOrderId, event.symbol_id,
                                event.side, event.price_ticks, event.quantity,
                                event.timestamp_ns, event.sequence_number});
  case MarketEventType::Heartbeat:
    return true;
  }
  return false;
}

PriceTicks reference_price_from_view(const L2View& view, const MarketDataEvent& event) noexcept {
  if (!view.bids.empty() && !view.asks.empty()) {
    return (view.bids.front().price_ticks + view.asks.front().price_ticks) / 2;
  }
  if (!view.bids.empty()) {
    return view.bids.front().price_ticks;
  }
  if (!view.asks.empty()) {
    return view.asks.front().price_ticks;
  }
  return event.price_ticks > 0 ? event.price_ticks : 1000;
}

template <typename Book>
struct HotPathContext {
  explicit HotPathContext(SymbolId symbol_id, std::size_t depth,
                          std::size_t expected_orders, std::size_t expected_risk_checks)
      : book(symbol_id), strategy(0.50, 1),
        risk(RiskLimits{1'000'000, 1'000'000'000, 1'000'000, 1'000'000'000'000,
                        1'000'000, 1'000'000'000}) {
    l2.reserve(depth);
    book.reserve_order_capacity(expected_orders);
    risk.reserve_hot_path_capacity(expected_risk_checks + 1U, 1U, 0U);
    risk.on_market_data(symbol_id, 1000, 0);
  }

  Book book;
  L2View l2;
  ImbalanceStrategy strategy;
  RiskGateway risk;
  ClientOrderId next_client_order_id{1};
  std::uint64_t guard{kFnvOffsetBasis};
  std::size_t risk_checks{0};
};

template <typename Book>
void replay_hot_path_once(std::span<const MarketDataEvent> events, HotPathContext<Book>& context,
                          std::size_t depth, std::vector<std::uint64_t>* samples) {
  context.book.clear();
  context.book.reserve_order_capacity(events.size());
  std::uint64_t activity_checksum = kFnvOffsetBasis;

  for (const MarketDataEvent& event : events) {
    const auto sample_start = std::chrono::steady_clock::now();
    const bool applied = apply_hot_path_event(event, context.book, activity_checksum);
    context.book.fill_l2_view(depth, context.l2);

    const PriceTicks reference_price = reference_price_from_view(context.l2, event);
    context.risk.on_market_data(event.symbol_id, reference_price, event.timestamp_ns);
    const StrategyDecisionBatch decisions = context.strategy.on_l2_update_fixed(context.l2);
    for (const StrategyDecision& decision : decisions) {
      const RiskResult risk_result = context.risk.check_new_order(
          NewOrderRequest{context.next_client_order_id,
                          event.symbol_id,
                          decision.side,
                          decision.order_type,
                          decision.price_ticks,
                          decision.quantity,
                          event.timestamp_ns,
                          1},
          event.timestamp_ns);
      context.guard = checksum_append(context.guard, context.next_client_order_id);
      context.guard = checksum_append(context.guard, risk_result.accepted ? 1U : 0U);
      context.guard = checksum_append(context.guard, risk_result.reject_reason);
      ++context.next_client_order_id;
      ++context.risk_checks;
    }
    context.guard = checksum_append(context.guard, applied ? 1U : 0U);
    context.guard = checksum_append(context.guard, static_cast<std::uint64_t>(context.l2.bids.size()));
    context.guard = checksum_append(context.guard, static_cast<std::uint64_t>(context.l2.asks.size()));
    context.guard = checksum_append(context.guard, activity_checksum);
    const auto sample_end = std::chrono::steady_clock::now();
    if (samples != nullptr) {
      samples->push_back(static_cast<std::uint64_t>(
          std::chrono::duration_cast<std::chrono::nanoseconds>(sample_end - sample_start).count()));
    }
  }

  context.guard = checksum_append(context.guard, context.book.checksum());
}

template <typename Book>
BenchmarkResult benchmark_hot_path_pipeline(const Options& options, std::string name) {
  constexpr std::size_t kDepth = 5;
  EventLogReadResult log = read_event_log(options.dataset_path, EventLogFormat::Auto);
  if (!log.error.empty()) {
    throw std::runtime_error("unable to load hot-path dataset: " + log.error);
  }
  if (log.events.empty()) {
    throw std::runtime_error("hot-path dataset is empty: " + options.dataset_path.string());
  }

  const std::size_t event_count = log.events.size() * options.hot_path_iterations;
  const std::size_t reserve_risk_checks =
      log.events.size() * (options.hot_path_iterations + options.warmup_iterations + 1U);
  HotPathContext<Book> context(log.events.front().symbol_id, kDepth, log.events.size(),
                               reserve_risk_checks);
  for (std::size_t i = 0; i < options.warmup_iterations; ++i) {
    replay_hot_path_once(log.events, context, kDepth, nullptr);
  }

  std::vector<std::uint64_t> samples;
  samples.reserve(event_count);
  const std::size_t risk_checks_before = context.risk_checks;
  context.guard = kFnvOffsetBasis;

  reset_allocation_counters();
  const auto start = std::chrono::steady_clock::now();
  for (std::size_t i = 0; i < options.hot_path_iterations; ++i) {
    replay_hot_path_once(log.events, context, kDepth, &samples);
  }
  const auto end = std::chrono::steady_clock::now();
  const AllocationSnapshot allocations = allocation_snapshot();

  const auto total_ns = static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count());
  BenchmarkResult result;
  result.name = std::move(name);
  result.iterations = options.hot_path_iterations;
  result.warmup_iterations = options.warmup_iterations;
  result.measured_iterations = options.hot_path_iterations;
  result.event_count = event_count;
  result.risk_check_count = context.risk_checks - risk_checks_before;
  result.dataset_name = options.dataset_path.filename().string();
  result.timing_mode = "per-event";
  result.total_ns = total_ns;
  result.avg_ns = event_count == 0 ? 0 : total_ns / event_count;
  result.throughput_events_per_second =
      total_ns == 0
          ? 0.0
          : static_cast<double>(event_count) * 1'000'000'000.0 / static_cast<double>(total_ns);
  result.guard = context.guard ^ log.event_checksum;
  result.allocations = allocations;
  result.latency = latency_distribution(std::move(samples));
  return result;
}

BenchmarkResult benchmark_add_order() {
  constexpr std::size_t kIterations = 50'000;
  OrderBook book(1);
  return run_benchmark("add_order", kIterations, [&] {
    for (std::size_t i = 0; i < kIterations; ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1U);
      (void)book.add_order(make_order(order_id, Side::Buy, 1000, 10));
    }
    return book.checksum();
  });
}

BenchmarkResult benchmark_cancel_order() {
  constexpr std::size_t kIterations = 50'000;
  OrderBook book(1);
  for (std::size_t i = 0; i < kIterations; ++i) {
    const OrderId order_id = static_cast<OrderId>(i + 1U);
    (void)book.add_order(make_order(order_id, Side::Sell, 1001, 10));
  }

  return run_benchmark("cancel_order", kIterations, [&] {
    for (std::size_t i = 0; i < kIterations; ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1U);
      (void)book.cancel_order(order_id);
    }
    return book.checksum();
  });
}

BenchmarkResult benchmark_replace_order() {
  constexpr std::size_t kIterations = 25'000;
  OrderBook book(1);
  for (std::size_t i = 0; i < kIterations; ++i) {
    const OrderId order_id = static_cast<OrderId>(i + 1U);
    (void)book.add_order(make_order(order_id, Side::Buy, 999, 10));
  }
  (void)book.add_order(make_order(900'000, Side::Buy, 1000, 1));
  (void)book.add_order(make_order(900'001, Side::Buy, 1001, 1));

  return run_benchmark("replace_order", kIterations, [&] {
    for (std::size_t i = 0; i < kIterations; ++i) {
      const OrderId order_id = static_cast<OrderId>(i + 1U);
      const PriceTicks new_price = 1000 + static_cast<PriceTicks>(i % 2U);
      (void)book.replace_order(order_id, new_price, 11, static_cast<TimestampNs>(i + 1U),
                               static_cast<SequenceNumber>(i + 1U));
    }
    return book.checksum();
  });
}

BenchmarkResult benchmark_market_cross_one_level() {
  constexpr std::size_t kIterations = 10'000;
  MatchingEngine engine(1);
  for (std::size_t i = 0; i < kIterations; ++i) {
    const ClientOrderId client_order_id = static_cast<ClientOrderId>(i + 1U);
    (void)engine.submit_order(NewOrderRequest{client_order_id, 1, Side::Sell, OrderType::Limit,
                                              1001, 10, static_cast<TimestampNs>(i + 1U)});
  }

  return run_benchmark("market_order_cross_one_level", kIterations, [&] {
    std::uint64_t guard = 0;
    for (std::size_t i = 0; i < kIterations; ++i) {
      const ClientOrderId client_order_id = static_cast<ClientOrderId>(1'000'000U + i);
      const auto reports = engine.submit_order(NewOrderRequest{
          client_order_id, 1, Side::Buy, OrderType::Market, 0, 10,
          static_cast<TimestampNs>(1'000'000U + i)});
      guard ^= static_cast<std::uint64_t>(reports.size());
    }
    return guard ^ engine.reports_checksum() ^ engine.book().checksum();
  });
}

BenchmarkResult benchmark_market_cross_multiple_levels() {
  constexpr std::size_t kIterations = 2'000;
  return run_benchmark("market_order_cross_multiple_levels", kIterations, [&] {
    std::uint64_t guard = 0;
    for (std::size_t i = 0; i < kIterations; ++i) {
      MatchingEngine engine(1);
      const ClientOrderId base = static_cast<ClientOrderId>(i * 10U + 1U);
      (void)engine.submit_order(
          NewOrderRequest{base, 1, Side::Sell, OrderType::Limit, 1001, 10, 1});
      (void)engine.submit_order(
          NewOrderRequest{base + 1U, 1, Side::Sell, OrderType::Limit, 1002, 10, 2});
      (void)engine.submit_order(
          NewOrderRequest{base + 2U, 1, Side::Sell, OrderType::Limit, 1003, 10, 3});
      const auto reports = engine.submit_order(
          NewOrderRequest{base + 3U, 1, Side::Buy, OrderType::Market, 0, 30, 4});
      guard ^= engine.reports_checksum();
      guard ^= engine.book().checksum();
      guard ^= static_cast<std::uint64_t>(reports.size());
    }
    return guard;
  });
}

BenchmarkResult benchmark_l2_snapshot() {
  constexpr std::size_t kIterations = 50'000;
  OrderBook book(1);
  for (std::size_t i = 0; i < 100U; ++i) {
    const OrderId bid_id = static_cast<OrderId>(i + 1U);
    const OrderId ask_id = static_cast<OrderId>(i + 10'001U);
    const PriceTicks offset = static_cast<PriceTicks>(i);
    (void)book.add_order(make_order(bid_id, Side::Buy, 1000 - offset, 10));
    (void)book.add_order(make_order(ask_id, Side::Sell, 1001 + offset, 10));
  }
  L2View view;
  view.reserve(25);

  return run_benchmark("l2_snapshot_generation", kIterations, [&] {
    std::uint64_t guard = 0;
    for (std::size_t i = 0; i < kIterations; ++i) {
      book.fill_l2_view(25, view);
      guard ^= static_cast<std::uint64_t>(view.bids.size() + view.asks.size());
      guard ^= static_cast<std::uint64_t>(view.bids.front().price_ticks);
      guard ^= static_cast<std::uint64_t>(view.asks.front().price_ticks);
    }
    return guard;
  });
}

BenchmarkResult benchmark_replay_sample_events(const std::filesystem::path& path) {
  return run_benchmark("replay_sample_events", 1, [&] {
    ReplayEngine replay(1);
    const ReplayResult result = replay.replay_file(path);
    std::uint64_t guard = result.final_book_checksum ^ result.execution_report_checksum;
    guard ^= static_cast<std::uint64_t>(result.events_processed);
    guard ^= result.sequence_valid ? 0x9e3779b97f4a7c15ULL : 0ULL;
    return guard;
  });
}

BenchmarkResult benchmark_risk_check_only() {
  constexpr std::size_t kIterations = 50'000;
  RiskGateway risk(RiskLimits{1'000, 2'000'000, 100'000, 100'000'000, 100, 1'000'000});
  risk.on_market_data(1, 1000, 100);

  return run_benchmark("risk_check_only", kIterations, [&] {
    std::uint64_t guard = 0;
    for (std::size_t i = 0; i < kIterations; ++i) {
      const auto result = risk.check_new_order(
          NewOrderRequest{static_cast<ClientOrderId>(i + 1U), 1, Side::Buy, OrderType::Limit,
                          1000, 1, static_cast<TimestampNs>(101 + i)},
          static_cast<TimestampNs>(101 + i));
      guard ^= result.accepted ? 1ULL : 0ULL;
      guard ^= static_cast<std::uint64_t>(result.reject_reason);
    }
    return guard;
  });
}

// Representative L2 view with both sides populated, shared by the feature
// extraction benchmarks. Built once so the benchmark measures extraction cost
// rather than book mutation.
L2View make_inference_l2_view() {
  L2View view;
  view.symbol_id = 1;
  view.bids.push_back(L2Level{999, 300});
  view.asks.push_back(L2Level{1001, 100});
  return view;
}

void tag_inference(BenchmarkResult& result, std::string backend, std::string model_name,
                   std::string input_shape, std::size_t feature_count = kL2FeatureCount,
                   std::uint32_t feature_version = kL2FeatureVersion) {
  result.category = "inference";
  result.backend = std::move(backend);
  result.model_name = std::move(model_name);
  result.input_shape = std::move(input_shape);
  result.feature_count = feature_count;
  result.feature_version = feature_version;
}

// Latency of a single timed iteration, recorded into the sample buffer.
inline void record_sample(std::vector<std::uint64_t>& samples,
                          std::chrono::steady_clock::time_point start,
                          std::chrono::steady_clock::time_point end) {
  samples.push_back(static_cast<std::uint64_t>(
      std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
}

BenchmarkResult benchmark_feature_extraction_vector_returning() {
  constexpr std::size_t kIterations = 200'000;
  const L2View view = make_inference_l2_view();
  FeatureExtractor extractor;

  BenchmarkResult result =
      run_sampled_benchmark("feature_extraction_vector_returning", kIterations,
                            [&](std::vector<std::uint64_t>& samples) {
                              std::uint64_t guard = 0;
                              for (std::size_t i = 0; i < kIterations; ++i) {
                                const auto start = std::chrono::steady_clock::now();
                                const std::vector<double> features = extractor.extract(view);
                                const auto end = std::chrono::steady_clock::now();
                                guard ^= static_cast<std::uint64_t>(features[0] * 1000.0);
                                record_sample(samples, start, end);
                              }
                              return guard;
                            });
  tag_inference(result, "n/a", "feature_extractor_v1", "1x4");
  return result;
}

BenchmarkResult benchmark_feature_extraction_caller_owned_buffer() {
  constexpr std::size_t kIterations = 200'000;
  const L2View view = make_inference_l2_view();
  FeatureExtractor extractor;
  std::array<double, kL2FeatureCount> feature_storage{};
  FeatureBuffer feature_buffer{feature_storage};

  BenchmarkResult result =
      run_sampled_benchmark("feature_extraction_caller_owned_buffer", kIterations,
                            [&](std::vector<std::uint64_t>& samples) {
                              std::uint64_t guard = 0;
                              for (std::size_t i = 0; i < kIterations; ++i) {
                                const auto start = std::chrono::steady_clock::now();
                                const FeatureExtractionStatus status =
                                    extractor.extract_into(view, feature_buffer);
                                const auto end = std::chrono::steady_clock::now();
                                guard ^= static_cast<std::uint64_t>(feature_storage[0] * 1000.0);
                                guard ^= status == FeatureExtractionStatus::Ok ? 0x9e37ULL : 0ULL;
                                record_sample(samples, start, end);
                              }
                              return guard;
                            });
  tag_inference(result, "n/a", "feature_extractor_v1_buffer", "1x4");
  return result;
}

BenchmarkResult benchmark_linear_inference_only() {
  constexpr std::size_t kIterations = 200'000;
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  const std::array<double, 4> features{2.0, 1000.0, 0.35, 400.0};

  BenchmarkResult result =
      run_sampled_benchmark("linear_inference_only", kIterations,
                            [&](std::vector<std::uint64_t>& samples) {
                              double accumulator = 0.0;
                              for (std::size_t i = 0; i < kIterations; ++i) {
                                const auto start = std::chrono::steady_clock::now();
                                accumulator += model.score(features);
                                const auto end = std::chrono::steady_clock::now();
                                record_sample(samples, start, end);
                              }
                              return static_cast<std::uint64_t>(accumulator * 1000.0);
                            });
  tag_inference(result, "linear", "linear_w4", "1x4");
  return result;
}

BenchmarkResult benchmark_feature_extraction_plus_linear_vector_returning() {
  constexpr std::size_t kIterations = 100'000;
  const L2View view = make_inference_l2_view();
  FeatureExtractor extractor;
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);

  BenchmarkResult result = run_sampled_benchmark(
      "feature_extraction_plus_linear_vector_returning", kIterations,
      [&](std::vector<std::uint64_t>& samples) {
        double accumulator = 0.0;
        for (std::size_t i = 0; i < kIterations; ++i) {
          const auto start = std::chrono::steady_clock::now();
          const std::vector<double> features = extractor.extract(view);
          accumulator += model.score(features);
          const auto end = std::chrono::steady_clock::now();
          record_sample(samples, start, end);
        }
        return static_cast<std::uint64_t>(accumulator * 1000.0);
      });
  tag_inference(result, "linear", "linear_w4", "1x4");
  return result;
}

BenchmarkResult benchmark_feature_extraction_plus_linear_caller_owned_buffer() {
  constexpr std::size_t kIterations = 100'000;
  const L2View view = make_inference_l2_view();
  FeatureExtractor extractor;
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  std::array<double, kL2FeatureCount> feature_storage{};
  FeatureBuffer feature_buffer{feature_storage};

  BenchmarkResult result = run_sampled_benchmark(
      "feature_extraction_plus_linear_caller_owned_buffer", kIterations,
      [&](std::vector<std::uint64_t>& samples) {
        double accumulator = 0.0;
        for (std::size_t i = 0; i < kIterations; ++i) {
          const auto start = std::chrono::steady_clock::now();
          const FeatureExtractionStatus status = extractor.extract_into(view, feature_buffer);
          accumulator +=
              status == FeatureExtractionStatus::Ok ? model.score(feature_buffer.used()) : 0.0;
          const auto end = std::chrono::steady_clock::now();
          record_sample(samples, start, end);
        }
        return static_cast<std::uint64_t>(accumulator * 1000.0);
      });
  tag_inference(result, "linear", "linear_w4", "1x4");
  return result;
}

BenchmarkResult benchmark_measured_linear_inference_only() {
  constexpr std::size_t kIterations = 50'000;
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  const std::array<double, 4> features{2.0, 1000.0, 0.35, 400.0};
  MeasuredInferenceEngine inference(model, InferencePolicy{1'000'000, 0, true, true});

  // The MeasuredInferenceEngine already times the model internally; we additionally
  // sample the end-to-end call (model score + policy accounting) per iteration.
  BenchmarkResult result =
      run_sampled_benchmark("measured_linear_inference_only", kIterations,
                            [&](std::vector<std::uint64_t>& samples) {
                              std::uint64_t guard = 0;
                              for (std::size_t i = 0; i < kIterations; ++i) {
                                const auto start = std::chrono::steady_clock::now();
                                const InferenceResult r = inference.score(features);
                                const auto end = std::chrono::steady_clock::now();
                                guard ^= static_cast<std::uint64_t>(r.score * 1000.0);
                                guard ^= r.accepted ? 0x9e3779b97f4a7c15ULL : 0ULL;
                                record_sample(samples, start, end);
                              }
                              return guard;
                            });
  tag_inference(result, "linear", "linear_w4", "1x4");
  return result;
}

BenchmarkResult benchmark_feature_buffer_measured_linear_inference() {
  constexpr std::size_t kIterations = 50'000;
  const L2View view = make_inference_l2_view();
  FeatureExtractor extractor;
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  MeasuredInferenceEngine inference(model, InferencePolicy{1'000'000, 0, true, true});
  std::array<double, kL2FeatureCount> feature_storage{};
  FeatureBuffer feature_buffer{feature_storage};

  BenchmarkResult result = run_sampled_benchmark(
      "feature_buffer_measured_linear_inference", kIterations,
      [&](std::vector<std::uint64_t>& samples) {
        std::uint64_t guard = 0;
        for (std::size_t i = 0; i < kIterations; ++i) {
          const auto start = std::chrono::steady_clock::now();
          const FeatureExtractionStatus status = extractor.extract_into(view, feature_buffer);
          const InferenceResult r =
              status == FeatureExtractionStatus::Ok ? inference.score(feature_buffer.used())
                                                    : InferenceResult{};
          const auto end = std::chrono::steady_clock::now();
          guard ^= static_cast<std::uint64_t>(r.score * 1000.0);
          guard ^= r.accepted ? 0x9e3779b97f4a7c15ULL : 0ULL;
          record_sample(samples, start, end);
        }
        return guard;
      });
  tag_inference(result, "linear", "linear_w4_policy", "1x4");
  return result;
}

// Event-loop policy overhead only: drives the stateful late-signal gate with
// injected timings (no wall-clock reads inside the gate, no model scoring) so the
// measurement isolates the timeout/late-signal accounting cost from the model.
BenchmarkResult benchmark_inference_policy_overhead() {
  constexpr std::size_t kIterations = 200'000;
  InferencePolicy policy;
  policy.timeout_ns = 1'000'000;
  policy.max_signal_age_ns = 1'000'000;
  InferencePolicyGate gate(policy);

  BenchmarkResult result =
      run_sampled_benchmark("inference_policy_overhead", kIterations,
                            [&](std::vector<std::uint64_t>& samples) {
                              std::uint64_t guard = 0;
                              for (std::size_t i = 0; i < kIterations; ++i) {
                                const auto stamp = static_cast<TimestampNs>(i);
                                const auto start = std::chrono::steady_clock::now();
                                // Injected latency under budget, age 0: always accepted.
                                const InferencePolicyResult pr = gate.observe(500, stamp, stamp);
                                const auto end = std::chrono::steady_clock::now();
                                guard ^= pr.accepted ? 0x9e3779b97f4a7c15ULL : 0ULL;
                                record_sample(samples, start, end);
                              }
                              return guard;
                            });
  tag_inference(result, "n/a", "policy_gate", "n/a", 0, 0);
  return result;
}

BenchmarkResult benchmark_feature_buffer_policy_gate_overhead() {
  constexpr std::size_t kIterations = 200'000;
  const L2View view = make_inference_l2_view();
  FeatureExtractor extractor;
  std::array<double, kL2FeatureCount> feature_storage{};
  FeatureBuffer feature_buffer{feature_storage};
  InferencePolicy policy;
  policy.timeout_ns = 1'000'000;
  policy.max_signal_age_ns = 1'000'000;
  InferencePolicyGate gate(policy);

  BenchmarkResult result =
      run_sampled_benchmark("feature_buffer_policy_gate_overhead", kIterations,
                            [&](std::vector<std::uint64_t>& samples) {
                              std::uint64_t guard = 0;
                              for (std::size_t i = 0; i < kIterations; ++i) {
                                const auto stamp = static_cast<TimestampNs>(i);
                                const auto start = std::chrono::steady_clock::now();
                                const FeatureExtractionStatus status =
                                    extractor.extract_into(view, feature_buffer);
                                const InferencePolicyResult pr = gate.observe(500, stamp, stamp);
                                const auto end = std::chrono::steady_clock::now();
                                guard ^= static_cast<std::uint64_t>(feature_storage[0] * 1000.0);
                                guard ^= pr.accepted ? 0x9e3779b97f4a7c15ULL : 0ULL;
                                guard ^= status == FeatureExtractionStatus::Ok ? 0x9e37ULL : 0ULL;
                                record_sample(samples, start, end);
                              }
                              return guard;
                            });
  tag_inference(result, "n/a", "feature_buffer_policy_gate", "1x4");
  return result;
}

#if defined(ASTERION_HAVE_ONNXRUNTIME)
std::vector<unsigned char> decode_base64_bytes(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("unable to read ONNX fixture: " + path.string());
  }
  std::string encoded((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  std::vector<unsigned char> output;
  int value = 0;
  int bits = -8;
  for (const unsigned char ch : encoded) {
    if (std::isspace(ch)) {
      continue;
    }
    if (ch == '=') {
      break;
    }
    int digit = -1;
    if (ch >= 'A' && ch <= 'Z') {
      digit = ch - 'A';
    } else if (ch >= 'a' && ch <= 'z') {
      digit = ch - 'a' + 26;
    } else if (ch >= '0' && ch <= '9') {
      digit = ch - '0' + 52;
    } else if (ch == '+') {
      digit = 62;
    } else if (ch == '/') {
      digit = 63;
    } else {
      continue;
    }
    value = (value << 6) + digit;
    bits += 6;
    if (bits >= 0) {
      output.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
      bits -= 8;
    }
  }
  return output;
}

std::filesystem::path materialise_onnx_fixture() {
  const auto bytes = decode_base64_bytes(std::filesystem::path(ASTERION_SOURCE_DIR) / "data" /
                                         "fixtures" / "identity_1x4.onnx.b64");
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("asterion_bench_identity_" + std::to_string(stamp) + ".onnx");
  std::ofstream output(path, std::ios::binary);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return path;
}

BenchmarkResult benchmark_onnx_inference_only(const std::filesystem::path& model_path) {
  constexpr std::size_t kIterations = 50'000;
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = model_path;
  config.linear_weights = {0.5, -0.001, 2.0, 0.0001};
  config.linear_bias = 1.0;
  InferenceBackendSelection selection = make_inference_backend(std::move(config));
  const std::array<double, 4> features{3.5, 1000.0, 0.35, 400.0};

  // Warm up so model-load/session-setup allocations are excluded from the
  // steady-state allocation count captured below.
  volatile double warm = selection.model->score(features);
  (void)warm;

  BenchmarkResult result =
      run_sampled_benchmark("onnx_inference_only", kIterations,
                            [&](std::vector<std::uint64_t>& samples) {
                              double accumulator = 0.0;
                              for (std::size_t i = 0; i < kIterations; ++i) {
                                const auto start = std::chrono::steady_clock::now();
                                accumulator += selection.model->score(features);
                                const auto end = std::chrono::steady_clock::now();
                                record_sample(samples, start, end);
                              }
                              return static_cast<std::uint64_t>(accumulator * 1000.0);
                            });
  tag_inference(result, std::string(to_string(selection.active)), "identity_1x4.onnx", "1x4");
  return result;
}

BenchmarkResult benchmark_feature_extraction_plus_onnx(const std::filesystem::path& model_path) {
  constexpr std::size_t kIterations = 50'000;
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = model_path;
  config.linear_weights = {0.5, -0.001, 2.0, 0.0001};
  config.linear_bias = 1.0;
  InferenceBackendSelection selection = make_inference_backend(std::move(config));
  const L2View view = make_inference_l2_view();
  FeatureExtractor extractor;

  volatile double warm = selection.model->score(extractor.extract(view));
  (void)warm;

  BenchmarkResult result = run_sampled_benchmark(
      "feature_extraction_plus_onnx_inference", kIterations,
      [&](std::vector<std::uint64_t>& samples) {
        double accumulator = 0.0;
        for (std::size_t i = 0; i < kIterations; ++i) {
          const auto start = std::chrono::steady_clock::now();
          const std::vector<double> features = extractor.extract(view);
          accumulator += selection.model->score(features);
          const auto end = std::chrono::steady_clock::now();
          record_sample(samples, start, end);
        }
        return static_cast<std::uint64_t>(accumulator * 1000.0);
      });
  tag_inference(result, std::string(to_string(selection.active)), "identity_1x4.onnx", "1x4");
  return result;
}

BenchmarkResult benchmark_feature_extraction_plus_onnx_caller_owned_buffer(
    const std::filesystem::path& model_path) {
  constexpr std::size_t kIterations = 50'000;
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = model_path;
  config.linear_weights = {0.5, -0.001, 2.0, 0.0001};
  config.linear_bias = 1.0;
  InferenceBackendSelection selection = make_inference_backend(std::move(config));
  const L2View view = make_inference_l2_view();
  FeatureExtractor extractor;
  std::array<double, kL2FeatureCount> feature_storage{};
  FeatureBuffer feature_buffer{feature_storage};

  (void)extractor.extract_into(view, feature_buffer);
  volatile double warm = selection.model->score(feature_buffer.used());
  (void)warm;

  BenchmarkResult result = run_sampled_benchmark(
      "feature_extraction_plus_onnx_caller_owned_buffer", kIterations,
      [&](std::vector<std::uint64_t>& samples) {
        double accumulator = 0.0;
        for (std::size_t i = 0; i < kIterations; ++i) {
          const auto start = std::chrono::steady_clock::now();
          const FeatureExtractionStatus status = extractor.extract_into(view, feature_buffer);
          accumulator +=
              status == FeatureExtractionStatus::Ok ? selection.model->score(feature_buffer.used())
                                                    : 0.0;
          const auto end = std::chrono::steady_clock::now();
          record_sample(samples, start, end);
        }
        return static_cast<std::uint64_t>(accumulator * 1000.0);
      });
  tag_inference(result, std::string(to_string(selection.active)), "identity_1x4.onnx", "1x4");
  return result;
}
#endif // ASTERION_HAVE_ONNXRUNTIME

void print_result(const BenchmarkResult& result) {
  std::cout << result.name << ",category=" << result.category
            << ",backend=" << (result.backend.empty() ? "n/a" : result.backend)
            << ",model=" << (result.model_name.empty() ? "n/a" : result.model_name)
            << ",input_shape=" << (result.input_shape.empty() ? "n/a" : result.input_shape)
            << ",feature_count=";
  if (result.feature_count > 0) {
    std::cout << result.feature_count << ",feature_version=" << result.feature_version;
  } else {
    std::cout << "n/a,feature_version=n/a";
  }
  std::cout << ",iterations=" << result.iterations
            << ",warmup_iterations=" << result.warmup_iterations
            << ",measured_iterations=" << result.measured_iterations
            << ",event_count=" << result.event_count
            << ",risk_check_count=" << result.risk_check_count
            << ",dataset=" << (result.dataset_name.empty() ? "n/a" : result.dataset_name)
            << ",timing_mode=" << result.timing_mode
            << ",total_ns=" << result.total_ns << ",avg_ns=" << result.avg_ns
            << ",p50_ns=";
  if (result.latency.available) {
    std::cout << result.latency.p50_ns << ",p95_ns=" << result.latency.p95_ns
              << ",p99_ns=" << result.latency.p99_ns
              << ",p999_ns=" << result.latency.p999_ns
              << ",max_ns=" << result.latency.max_ns;
  } else {
    std::cout << "n/a,p95_ns=n/a,p99_ns=n/a,p999_ns=n/a,max_ns=n/a";
  }
  std::cout << ",throughput_events_per_second=" << std::fixed << std::setprecision(2)
            << result.throughput_events_per_second << std::defaultfloat
            << ",allocations=" << result.allocations.allocations
            << ",deallocations=" << result.allocations.deallocations
            << ",bytes_allocated=" << result.allocations.bytes_allocated
            << ",guard=" << result.guard << '\n';
}

std::string json_escape(std::string_view value) {
  std::ostringstream output;
  for (const char c : value) {
    switch (c) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      output << c;
      break;
    }
  }
  return output.str();
}

std::string cpu_name() {
#if defined(_WIN32)
  const char* processor = std::getenv("PROCESSOR_IDENTIFIER");
  return processor == nullptr ? "unknown" : std::string(processor);
#elif defined(__linux__)
  std::ifstream input("/proc/cpuinfo");
  std::string line;
  while (std::getline(input, line)) {
    const std::string marker = "model name";
    if (line.rfind(marker, 0) == 0) {
      const auto colon = line.find(':');
      if (colon != std::string::npos && colon + 2U < line.size()) {
        return line.substr(colon + 2U);
      }
    }
  }
  return "unknown";
#else
  return "unknown";
#endif
}

std::string os_name() {
#if defined(_WIN32)
  return "Windows";
#elif defined(__linux__) || defined(__APPLE__)
  utsname info{};
  if (uname(&info) == 0) {
    std::ostringstream output;
    output << info.sysname << ' ' << info.release << ' ' << info.machine;
    return output.str();
  }
  return "unknown";
#else
  return "unknown";
#endif
}

std::string compiler_name() {
#if defined(__clang__)
  return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
  return std::string("GCC ") + __VERSION__;
#elif defined(_MSC_VER)
  return "MSVC " + std::to_string(_MSC_VER);
#else
  return "unknown";
#endif
}

void write_optional_latency(std::ostream& output, const char* key,
                            const LatencyDistribution& latency,
                            std::uint64_t LatencyDistribution::*field) {
  output << "      \"" << key << "\": ";
  if (latency.available) {
    output << latency.*field;
  } else {
    output << "null";
  }
  output << ",\n";
}

void write_json(const std::filesystem::path& path, const Options& options,
                const std::vector<BenchmarkResult>& results) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("unable to write benchmark JSON: " + path.string());
  }

  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"environment\": {\n";
  output << "    \"cpu\": \"" << json_escape(cpu_name()) << "\",\n";
  output << "    \"os\": \"" << json_escape(os_name()) << "\",\n";
  output << "    \"compiler\": \"" << json_escape(compiler_name()) << "\",\n";
  output << "    \"build_type\": \"" << json_escape(ASTERION_BUILD_TYPE) << "\",\n";
  output << "    \"compiler_flags\": \"" << json_escape(ASTERION_COMPILER_FLAGS) << "\",\n";
  output << "    \"commit_hash\": \"" << json_escape(ASTERION_GIT_COMMIT) << "\",\n";
  output << "    \"dataset\": \"" << json_escape(options.dataset_path.string()) << "\",\n";
  output << "    \"logging_mode\": \"" << json_escape(options.logging_mode) << "\"\n";
  output << "  },\n";
  output << "  \"benchmarks\": [\n";
  for (std::size_t i = 0; i < results.size(); ++i) {
    const BenchmarkResult& result = results[i];
    output << "    {\n";
    output << "      \"name\": \"" << json_escape(result.name) << "\",\n";
    output << "      \"category\": \"" << json_escape(result.category) << "\",\n";
    output << "      \"backend\": \"" << json_escape(result.backend) << "\",\n";
    output << "      \"model_name\": \"" << json_escape(result.model_name) << "\",\n";
    output << "      \"input_shape\": \"" << json_escape(result.input_shape) << "\",\n";
    output << "      \"feature_count\": ";
    if (result.feature_count > 0) {
      output << result.feature_count;
    } else {
      output << "null";
    }
    output << ",\n";
    output << "      \"feature_version\": ";
    if (result.feature_version > 0) {
      output << result.feature_version;
    } else {
      output << "null";
    }
    output << ",\n";
    output << "      \"iterations\": " << result.iterations << ",\n";
    output << "      \"warmup_iterations\": " << result.warmup_iterations << ",\n";
    output << "      \"measured_iterations\": " << result.measured_iterations << ",\n";
    output << "      \"event_count\": " << result.event_count << ",\n";
    output << "      \"risk_check_count\": " << result.risk_check_count << ",\n";
    output << "      \"dataset_name\": \"" << json_escape(result.dataset_name) << "\",\n";
    output << "      \"timing_mode\": \"" << json_escape(result.timing_mode) << "\",\n";
    output << "      \"total_ns\": " << result.total_ns << ",\n";
    output << "      \"avg_ns\": " << result.avg_ns << ",\n";
    write_optional_latency(output, "p50_ns", result.latency, &LatencyDistribution::p50_ns);
    write_optional_latency(output, "p95_ns", result.latency, &LatencyDistribution::p95_ns);
    write_optional_latency(output, "p99_ns", result.latency, &LatencyDistribution::p99_ns);
    write_optional_latency(output, "p999_ns", result.latency, &LatencyDistribution::p999_ns);
    write_optional_latency(output, "max_ns", result.latency, &LatencyDistribution::max_ns);
    output << "      \"throughput_events_per_second\": " << std::fixed << std::setprecision(2)
           << result.throughput_events_per_second << std::defaultfloat << ",\n";
    output << "      \"guard\": " << result.guard << ",\n";
    output << "      \"allocations\": " << result.allocations.allocations << ",\n";
    output << "      \"deallocations\": " << result.allocations.deallocations << ",\n";
    output << "      \"bytes_allocated\": " << result.allocations.bytes_allocated << "\n";
    output << "    }" << (i + 1U == results.size() ? "\n" : ",\n");
  }
  output << "  ]\n";
  output << "}\n";
}

} // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    return 1;
  }

  // Core replay/book/matching/risk benchmarks. These timings are kept separate
  // from the inference timings below.
  std::vector<BenchmarkResult> core_results;
  core_results.push_back(benchmark_hot_path_pipeline<OrderBook>(
      options, "hot_path_binary_replay_l3_l2_strategy_risk"));
  core_results.push_back(benchmark_hot_path_pipeline<PooledOrderBook>(
      options, "hot_path_binary_replay_pooled_l3_l2_strategy_risk"));
  if (!options.only_hot_path) {
    core_results.push_back(benchmark_add_order());
    core_results.push_back(benchmark_cancel_order());
    core_results.push_back(benchmark_replace_order());
    core_results.push_back(benchmark_market_cross_one_level());
    core_results.push_back(benchmark_market_cross_multiple_levels());
    core_results.push_back(benchmark_l2_snapshot());
    core_results.push_back(benchmark_replay_sample_events(options.dataset_path));
    core_results.push_back(benchmark_risk_check_only());
  }

  // Inference benchmarks. Feature extraction and model scoring are measured on
  // their own so inference cost is never folded into the trading hot path.
  std::vector<BenchmarkResult> inference_results;
  if (!options.only_hot_path) {
    inference_results.push_back(benchmark_feature_extraction_vector_returning());
    inference_results.push_back(benchmark_feature_extraction_caller_owned_buffer());
    inference_results.push_back(benchmark_linear_inference_only());
    inference_results.push_back(benchmark_feature_extraction_plus_linear_vector_returning());
    inference_results.push_back(benchmark_feature_extraction_plus_linear_caller_owned_buffer());
    inference_results.push_back(benchmark_measured_linear_inference_only());
    inference_results.push_back(benchmark_feature_buffer_measured_linear_inference());
    inference_results.push_back(benchmark_inference_policy_overhead());
    inference_results.push_back(benchmark_feature_buffer_policy_gate_overhead());
#if defined(ASTERION_HAVE_ONNXRUNTIME)
    // The ONNX benchmarks only build/run when the opt-in dependency is present.
    // The tiny identity fixture is decoded to a temp file and removed afterwards.
    std::filesystem::path onnx_model_path;
    try {
      onnx_model_path = materialise_onnx_fixture();
      inference_results.push_back(benchmark_onnx_inference_only(onnx_model_path));
      inference_results.push_back(benchmark_feature_extraction_plus_onnx(onnx_model_path));
      inference_results.push_back(
          benchmark_feature_extraction_plus_onnx_caller_owned_buffer(onnx_model_path));
    } catch (const std::exception& ex) {
      std::cerr << "onnx benchmark skipped: " << ex.what() << '\n';
    }
    if (!onnx_model_path.empty()) {
      std::error_code remove_ec;
      std::filesystem::remove(onnx_model_path, remove_ec);
    }
#endif
  }

  std::vector<BenchmarkResult> results;
  results.reserve(core_results.size() + inference_results.size());
  results.insert(results.end(), core_results.begin(), core_results.end());
  results.insert(results.end(), inference_results.begin(), inference_results.end());

  if (options.text_output) {
    std::cout << "# core (replay / book / matching / risk)\n";
    for (const BenchmarkResult& result : core_results) {
      print_result(result);
    }
    if (!inference_results.empty()) {
      std::cout << "# inference (feature extraction / model scoring, measured separately)\n";
      for (const BenchmarkResult& result : inference_results) {
        print_result(result);
      }
    }
  }

  if (options.json_path.has_value()) {
    write_json(*options.json_path, options, results);
  }

  return 0;
}
