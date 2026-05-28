#include "asterion/book/order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
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
                                     "samples" / "sample_replay.csv"};
  std::optional<std::filesystem::path> json_path;
  bool text_output{true};
  std::string logging_mode;
};

struct BenchmarkResult {
  std::string name;
  std::size_t iterations{0};
  std::uint64_t total_ns{0};
  std::uint64_t avg_ns{0};
  std::uint64_t guard{0};
  AllocationSnapshot allocations;
};

Order make_order(OrderId order_id, Side side, PriceTicks price, Quantity quantity) {
  return Order{order_id, order_id + 1'000'000, 1, side, price, quantity,
               static_cast<TimestampNs>(order_id), order_id};
}

void print_usage(std::ostream& output) {
  output << "Usage: asterion_benchmarks [dataset.csv] [--dataset path] [--json path]"
         << " [--no-text] [--logging-mode name]\n";
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
    if (arg == "--logging-mode") {
      if (i + 1 >= argc) {
        std::cerr << "--logging-mode requires a value\n";
        return false;
      }
      options.logging_mode = argv[++i];
      logging_mode_overridden = true;
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
  return BenchmarkResult{std::move(name),
                         iterations,
                         total_ns,
                         iterations == 0 ? 0 : total_ns / iterations,
                         guard,
                         allocations};
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

  return run_benchmark("l2_snapshot_generation", kIterations, [&] {
    std::uint64_t guard = 0;
    for (std::size_t i = 0; i < kIterations; ++i) {
      const L2View view = book.l2_view(25);
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

BenchmarkResult benchmark_linear_inference_only() {
  constexpr std::size_t kIterations = 200'000;
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  const std::array<double, 4> features{2.0, 1000.0, 0.35, 400.0};

  return run_benchmark("linear_inference_only", kIterations, [&] {
    double accumulator = 0.0;
    for (std::size_t i = 0; i < kIterations; ++i) {
      accumulator += model.score(features);
    }
    return static_cast<std::uint64_t>(accumulator * 1000.0);
  });
}

BenchmarkResult benchmark_measured_linear_inference_only() {
  constexpr std::size_t kIterations = 50'000;
  LinearModel model({0.5, -0.001, 2.0, 0.0001}, 1.0);
  const std::array<double, 4> features{2.0, 1000.0, 0.35, 400.0};
  MeasuredInferenceEngine inference(model, InferencePolicy{1'000'000, 0, true, true});

  return run_benchmark("measured_linear_inference_only", kIterations, [&] {
    std::uint64_t guard = 0;
    for (std::size_t i = 0; i < kIterations; ++i) {
      const InferenceResult result = inference.score(features);
      guard ^= static_cast<std::uint64_t>(result.score * 1000.0);
      guard ^= result.inference_latency_ns;
      guard ^= result.accepted ? 0x9e3779b97f4a7c15ULL : 0ULL;
    }
    return guard;
  });
}

void print_result(const BenchmarkResult& result) {
  std::cout << result.name << ",iterations=" << result.iterations
            << ",total_ns=" << result.total_ns << ",avg_ns=" << result.avg_ns
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
    output << "      \"iterations\": " << result.iterations << ",\n";
    output << "      \"total_ns\": " << result.total_ns << ",\n";
    output << "      \"avg_ns\": " << result.avg_ns << ",\n";
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

  std::vector<BenchmarkResult> results;
  results.reserve(10);
  results.push_back(benchmark_add_order());
  results.push_back(benchmark_cancel_order());
  results.push_back(benchmark_replace_order());
  results.push_back(benchmark_market_cross_one_level());
  results.push_back(benchmark_market_cross_multiple_levels());
  results.push_back(benchmark_l2_snapshot());
  results.push_back(benchmark_replay_sample_events(options.dataset_path));
  results.push_back(benchmark_risk_check_only());
  results.push_back(benchmark_linear_inference_only());
  results.push_back(benchmark_measured_linear_inference_only());

  if (options.text_output) {
    for (const BenchmarkResult& result : results) {
      print_result(result);
    }
  }

  if (options.json_path.has_value()) {
    write_json(*options.json_path, options, results);
  }

  return 0;
}
