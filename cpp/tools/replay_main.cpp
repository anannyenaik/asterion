#include "asterion/market_data/event_log.hpp"
#include "asterion/market_data/replay.hpp"

#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

using namespace asterion;

namespace {

struct Options {
  std::filesystem::path input_path;
  EventLogFormat format{EventLogFormat::Auto};
  SymbolId symbol_id{1};
  bool print_diagnostics{true};
};

void print_usage(std::ostream& output) {
  output << "Usage: asterion_replay --input path [--format auto|csv|binary] [--symbol id]"
         << " [--no-diagnostics]\n";
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
      options.symbol_id = static_cast<SymbolId>(std::stoul(argv[++i]));
      continue;
    }
    if (arg == "--no-diagnostics") {
      options.print_diagnostics = false;
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

} // namespace

int main(int argc, char** argv) {
  Options options;
  if (!parse_options(argc, argv, options)) {
    return 1;
  }

  ReplayEngine replay(options.symbol_id);
  const ReplayResult result = replay.replay_file(options.input_path, options.format);
  print_result(result);
  if (options.print_diagnostics) {
    print_diagnostics(result);
  }
  return result.sequence_valid && result.error.empty() ? 0 : 2;
}
