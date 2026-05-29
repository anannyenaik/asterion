#include "asterion/book/order_book.hpp"
#include "asterion/core/clock.hpp"
#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/matching/matching_engine.hpp"
#include "asterion/risk/risk_gateway.hpp"
#include "asterion/strategy/market_maker.hpp"
#include "asterion/telemetry/latency_budget.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#ifndef ASTERION_SOURCE_DIR
#define ASTERION_SOURCE_DIR "."
#endif

using namespace asterion;

namespace {

struct Options {
  std::filesystem::path dataset_path{std::filesystem::path(ASTERION_SOURCE_DIR) / "data" /
                                     "samples" / "sample_replay.csv"};
  std::optional<std::filesystem::path> json_path;
  bool text_output{true};
  std::size_t iterations{1000};
  LatencyBudgetConfig budget;
};

void print_usage(std::ostream& output) {
  output << "Usage: asterion_latency_budget [--dataset path] [--json path] [--no-text]"
         << " [--iterations N]\n"
         << "       [--replay-budget-ns N] [--book-budget-ns N] [--matching-budget-ns N]\n"
         << "       [--risk-budget-ns N] [--strategy-budget-ns N] [--inference-budget-ns N]\n"
         << "       [--total-budget-ns N]\n";
}

[[nodiscard]] bool parse_u64(const char* text, std::uint64_t& out) {
  try {
    std::size_t parsed = 0;
    out = static_cast<std::uint64_t>(std::stoull(text, &parsed, 10));
    if (parsed != std::string_view(text).size()) {
      return false;
    }
  } catch (const std::exception&) {
    return false;
  }
  return true;
}

bool parse_options(int argc, char** argv, Options& options) {
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
    if (arg == "--iterations") {
      if (i + 1 >= argc) {
        std::cerr << "--iterations requires a value\n";
        return false;
      }
      std::uint64_t value = 0;
      if (!parse_u64(argv[++i], value) || value == 0) {
        std::cerr << "--iterations requires a positive integer\n";
        return false;
      }
      options.iterations = static_cast<std::size_t>(value);
      continue;
    }

    static const std::array<std::pair<std::string_view, LatencyStage>, 7> kBudgetFlags{
        {{"--replay-budget-ns", LatencyStage::Replay},
         {"--book-budget-ns", LatencyStage::BookUpdate},
         {"--matching-budget-ns", LatencyStage::Matching},
         {"--risk-budget-ns", LatencyStage::Risk},
         {"--strategy-budget-ns", LatencyStage::Strategy},
         {"--inference-budget-ns", LatencyStage::Inference},
         {"--total-budget-ns", LatencyStage::Total}}};

    bool handled = false;
    for (const auto& [flag, stage] : kBudgetFlags) {
      if (arg == flag) {
        if (i + 1 >= argc) {
          std::cerr << flag << " requires a value\n";
          return false;
        }
        std::uint64_t value = 0;
        if (!parse_u64(argv[++i], value)) {
          std::cerr << flag << " requires an integer nanosecond budget\n";
          return false;
        }
        options.budget.set_budget(stage, value);
        handled = true;
        break;
      }
    }
    if (handled) {
      continue;
    }

    if (!arg.empty() && arg.front() == '-') {
      std::cerr << "unknown option: " << arg << '\n';
      return false;
    }
    options.dataset_path = std::filesystem::path(argv[i]);
  }
  return true;
}

[[nodiscard]] std::uint64_t elapsed_since(TimestampNs start_ns) {
  const TimestampNs now = monotonic_now_ns();
  const TimestampNs delta = now - start_ns;
  return delta > 0 ? static_cast<std::uint64_t>(delta) : 0;
}

NewOrderRequest make_risk_request(std::size_t i) {
  return NewOrderRequest{static_cast<ClientOrderId>(i + 1U),
                         1,
                         Side::Buy,
                         OrderType::Limit,
                         1000,
                         1,
                         static_cast<TimestampNs>(1'000 + i)};
}

bool measure_pipeline(const Options& options, LatencyBudgetAccountant& accountant) {
  bool pipeline_ok = true;

  // Replay: decode and reconstruct the book from the recorded sample log.
  {
    ReplayEngine replay(1);
    const TimestampNs start = monotonic_now_ns();
    const ReplayResult result = replay.replay_file(options.dataset_path);
    const std::uint64_t elapsed = elapsed_since(start);
    accountant.record(LatencyStage::Replay, elapsed);
    if (!result.error.empty()) {
      std::cerr << "replay error: " << result.error << '\n';
      pipeline_ok = false;
    }
  }

  // Book update: time individual resting-order insertions.
  {
    OrderBook book(1);
    for (std::size_t i = 0; i < options.iterations; ++i) {
      const Order order{static_cast<OrderId>(i + 1U), 0, 1, Side::Buy,
                        1000 - static_cast<PriceTicks>(i % 8U), 10,
                        static_cast<TimestampNs>(i + 1U), static_cast<SequenceNumber>(i + 1U)};
      const TimestampNs start = monotonic_now_ns();
      const bool ok = book.add_order(order);
      accountant.record(LatencyStage::BookUpdate, elapsed_since(start));
      (void)ok;
    }
  }

  // Matching: time a marketable buy crossing one resting level.
  {
    for (std::size_t i = 0; i < options.iterations; ++i) {
      MatchingEngine engine(1);
      (void)engine.submit_order(NewOrderRequest{static_cast<ClientOrderId>(2U * i + 1U), 1,
                                                Side::Sell, OrderType::Limit, 1001, 10, 1});
      const NewOrderRequest cross{static_cast<ClientOrderId>(2U * i + 2U), 1, Side::Buy,
                                  OrderType::Market, 0, 10, 2};
      const TimestampNs start = monotonic_now_ns();
      const auto reports = engine.submit_order(cross);
      accountant.record(LatencyStage::Matching, elapsed_since(start));
      (void)reports;
    }
  }

  // Risk: time a single pre-trade check.
  {
    RiskGateway risk(RiskLimits{1'000, 2'000'000, 100'000, 100'000'000, 100, 0});
    risk.on_market_data(1, 1000, 0);
    for (std::size_t i = 0; i < options.iterations; ++i) {
      const NewOrderRequest request = make_risk_request(i);
      const TimestampNs start = monotonic_now_ns();
      const RiskResult result = risk.check_new_order(request, request.timestamp_ns);
      accountant.record(LatencyStage::Risk, elapsed_since(start));
      (void)result;
    }
  }

  // Strategy and inference share a prepared top-of-book view and feature vector.
  L2View view;
  view.symbol_id = 1;
  view.bids.push_back(L2Level{999, 300});
  view.asks.push_back(L2Level{1003, 100});

  {
    MarketMaker maker(5);
    for (std::size_t i = 0; i < options.iterations; ++i) {
      const TimestampNs start = monotonic_now_ns();
      const auto quotes = maker.on_l2_update(view);
      accountant.record(LatencyStage::Strategy, elapsed_since(start));
      (void)quotes;
    }
  }

  {
    FeatureExtractor extractor;
    const std::vector<double> features = extractor.extract(view);
    LinearModel model({0.5, 0.0, 2.0, 0.001}, 1.0);
    for (std::size_t i = 0; i < options.iterations; ++i) {
      const TimestampNs start = monotonic_now_ns();
      const double score = model.score(features);
      accountant.record(LatencyStage::Inference, elapsed_since(start));
      (void)score;
    }
  }

  // Total: one combined book + strategy + risk + matching + inference pass per event.
  {
    FeatureExtractor extractor;
    LinearModel model({0.5, 0.0, 2.0, 0.001}, 1.0);
    MarketMaker maker(5);
    RiskGateway risk(RiskLimits{1'000, 2'000'000, 100'000, 100'000'000, 100, 0});
    risk.on_market_data(1, 1000, 0);
    for (std::size_t i = 0; i < options.iterations; ++i) {
      MatchingEngine engine(1);
      (void)engine.submit_order(NewOrderRequest{static_cast<ClientOrderId>(4U * i + 1U), 1,
                                                Side::Sell, OrderType::Limit, 1001, 10, 1});
      const std::vector<double> features = extractor.extract(view);
      const NewOrderRequest request = make_risk_request(i);

      const TimestampNs start = monotonic_now_ns();
      const auto quotes = maker.on_l2_update(view);
      const RiskResult risk_result = risk.check_new_order(request, request.timestamp_ns);
      const auto reports = engine.submit_order(NewOrderRequest{
          static_cast<ClientOrderId>(4U * i + 2U), 1, Side::Buy, OrderType::Market, 0, 10, 2});
      const double score = model.score(features);
      accountant.record(LatencyStage::Total, elapsed_since(start));
      (void)quotes;
      (void)risk_result;
      (void)reports;
      (void)score;
    }
  }

  return pipeline_ok;
}

void print_text(const LatencyBudgetAccountant& accountant) {
  for (const StageBudgetReport& report : accountant.reports()) {
    std::cout << "stage=" << to_string(report.stage) << ",samples=" << report.sample_count
              << ",worst_ns=" << report.worst_observed_ns
              << ",total_ns=" << report.total_observed_ns
              << ",budget_ns=" << report.budget_ns
              << ",has_budget=" << (report.has_budget ? "true" : "false")
              << ",utilization_ppm=" << report.worst_utilization_ppm
              << ",exceeded=" << (report.exceeded ? "true" : "false") << '\n';
  }
  std::cout << "exceeded_count=" << accountant.exceeded_count() << '\n';
  const std::optional<LatencyStage> worst = accountant.worst_offender();
  const std::string_view worst_name =
      worst.has_value() ? to_string(*worst) : std::string_view("none");
  std::cout << "worst_offender=" << worst_name << '\n';
  std::cout << "config_checksum=" << accountant.config_checksum() << '\n';
}

void write_json(const std::filesystem::path& path, const LatencyBudgetAccountant& accountant) {
  std::ofstream output(path);
  if (!output) {
    throw std::runtime_error("unable to write latency-budget JSON: " + path.string());
  }
  output << latency_budget_json(accountant);
}

} // namespace

int main(int argc, char** argv) {
  if (argc == 2) {
    const std::string_view arg(argv[1]);
    if (arg == "--help" || arg == "-h") {
      print_usage(std::cout);
      return 0;
    }
  }

  Options options;
  if (!parse_options(argc, argv, options)) {
    return 1;
  }

  LatencyBudgetAccountant accountant(options.budget);
  const bool measured = measure_pipeline(options, accountant);

  if (options.text_output) {
    print_text(accountant);
  }
  if (options.json_path.has_value()) {
    try {
      write_json(*options.json_path, accountant);
    } catch (const std::exception& exc) {
      std::cerr << exc.what() << '\n';
      return 1;
    }
  }

  if (!measured) {
    return 2;
  }
  return accountant.exceeded_count() == 0 ? 0 : 3;
}
