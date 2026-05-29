#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"
#include "asterion/market_data/replay_aggregate.hpp"

#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>

using namespace asterion;

namespace {

struct Options {
  std::filesystem::path input_path;
  EventLogFormat format{EventLogFormat::Auto};
  SymbolId symbol_id{1};
  bool print_diagnostics{true};
  bool aggregate{false};
  bool shared{false};
};

void print_usage(std::ostream& output) {
  output << "Usage: asterion_replay --input path [--format auto|csv|binary] [--symbol id]"
         << " [--aggregate] [--shared] [--no-diagnostics]\n";
}

bool parse_symbol_id(const char* text, SymbolId& output) {
  try {
    std::size_t parsed = 0;
    const unsigned long value = std::stoul(text, &parsed, 10);
    if (parsed != std::string_view(text).size() || value == 0 ||
        value > std::numeric_limits<SymbolId>::max()) {
      return false;
    }
    output = static_cast<SymbolId>(value);
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
    if (arg == "--input") {
      if (i + 1 >= argc) {
        std::cerr << "--input requires a path\n";
        return false;
      }
      options.input_path = argv[++i];
      continue;
    }
    if (arg == "--format") {
      if (i + 1 >= argc) {
        std::cerr << "--format requires auto, csv or binary\n";
        return false;
      }
      const auto format = parse_event_log_format(argv[++i]);
      if (!format) {
        std::cerr << "unknown format: " << argv[i] << '\n';
        return false;
      }
      options.format = *format;
      continue;
    }
    if (arg == "--symbol") {
      if (i + 1 >= argc) {
        std::cerr << "--symbol requires an integer symbol id\n";
        return false;
      }
      if (!parse_symbol_id(argv[++i], options.symbol_id)) {
        std::cerr << "--symbol requires a positive integer symbol id\n";
        return false;
      }
      continue;
    }
    if (arg == "--no-diagnostics") {
      options.print_diagnostics = false;
      continue;
    }
    if (arg == "--aggregate") {
      options.aggregate = true;
      continue;
    }
    if (arg == "--shared") {
      options.aggregate = true;
      options.shared = true;
      continue;
    }
    if (!arg.empty() && arg.front() == '-') {
      std::cerr << "unknown option: " << arg << '\n';
      return false;
    }
    options.input_path = std::filesystem::path(argv[i]);
  }

  if (options.input_path.empty()) {
    std::cerr << "input path is required\n";
    print_usage(std::cerr);
    return false;
  }
  return true;
}

void print_result(const ReplayResult& result) {
  std::cout << "events_processed=" << result.events_processed << '\n';
  std::cout << "sequence_valid=" << (result.sequence_valid ? "true" : "false") << '\n';
  std::cout << "event_log_checksum=" << result.event_log_checksum << '\n';
  std::cout << "final_book_checksum=" << result.final_book_checksum << '\n';
  std::cout << "execution_report_checksum=" << result.execution_report_checksum << '\n';
  std::cout << "diagnostics_checksum=" << result.diagnostics_checksum << '\n';
  std::cout << "diagnostic_errors=" << result.diagnostic_error_count << '\n';
  std::cout << "diagnostic_warnings=" << result.diagnostic_warning_count << '\n';
  std::cout << "diagnostic_count=" << result.diagnostics.size() << '\n';
  if (!result.error.empty()) {
    std::cout << "error=" << result.error << '\n';
  }
}

void print_diagnostics(const ReplayResult& result) {
  for (const ReplayDiagnostic& diagnostic : result.diagnostics) {
    std::cout << "diagnostic,event_index=" << diagnostic.event_index
              << ",sequence_number=" << diagnostic.sequence_number
              << ",symbol=" << diagnostic.symbol_id
              << ",severity=" << to_string(diagnostic.severity)
              << ",reason=" << diagnostic.reason << '\n';
  }
}

void print_aggregate_result(const AggregateReplaySummary& summary, bool shared) {
  std::cout << "path=" << (shared ? "shared" : "grouped") << '\n';
  std::cout << "total_events=" << summary.total_events << '\n';
  std::cout << "symbol_count=" << summary.symbol_count << '\n';
  std::cout << "combined_book_checksum=" << summary.combined_book_checksum << '\n';
  std::cout << "aggregate_checksum=" << summary.aggregate_checksum << '\n';
  if (!summary.error.empty()) {
    std::cout << "error=" << summary.error << '\n';
  }
  for (const SymbolReplaySummary& symbol : summary.symbols) {
    std::cout << "symbol=" << symbol.symbol_id << ",events=" << symbol.event_count
              << ",first_sequence=" << symbol.first_sequence
              << ",last_sequence=" << symbol.last_sequence
              << ",sequence_valid=" << (symbol.sequence_valid ? "true" : "false")
              << ",final_book_checksum=" << symbol.final_book_checksum
              << ",execution_report_checksum=" << symbol.execution_report_checksum
              << ",diagnostics_checksum=" << symbol.diagnostics_checksum
              << ",diagnostic_errors=" << symbol.diagnostic_error_count << '\n';
  }
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

  if (options.aggregate) {
    const AggregateReplaySummary summary =
        options.shared ? replay_file_shared_by_symbol(options.input_path, options.format)
                       : replay_file_by_symbol(options.input_path, options.format);
    print_aggregate_result(summary, options.shared);
    return summary.error.empty() ? 0 : 2;
  }

  ReplayEngine replay(options.symbol_id);
  const ReplayResult result = replay.replay_file(options.input_path, options.format);
  print_result(result);
  if (options.print_diagnostics) {
    print_diagnostics(result);
  }
  return result.sequence_valid && result.error.empty() ? 0 : 2;
}
