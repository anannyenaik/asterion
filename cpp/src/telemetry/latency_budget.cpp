#include "asterion/telemetry/latency_budget.hpp"

#include "asterion/core/checksum.hpp"

#include <sstream>

namespace asterion {

namespace {

constexpr std::array<LatencyStage, LatencyBudgetAccountant::kStageCount> kStageOrder{
    LatencyStage::Replay,   LatencyStage::BookUpdate, LatencyStage::Matching,
    LatencyStage::Risk,     LatencyStage::Strategy,   LatencyStage::Inference,
    LatencyStage::Total};

[[nodiscard]] std::uint64_t utilization_ppm(std::uint64_t observed_ns,
                                            std::uint64_t budget_ns) noexcept {
  if (budget_ns == 0) {
    return 0;
  }
  return (observed_ns * 1'000'000ULL) / budget_ns;
}

[[nodiscard]] std::string json_escape(std::string_view value) {
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

} // namespace

std::string_view to_string(LatencyStage stage) noexcept {
  switch (stage) {
  case LatencyStage::Replay:
    return "replay";
  case LatencyStage::BookUpdate:
    return "book_update";
  case LatencyStage::Matching:
    return "matching";
  case LatencyStage::Risk:
    return "risk";
  case LatencyStage::Strategy:
    return "strategy";
  case LatencyStage::Inference:
    return "inference";
  case LatencyStage::Total:
    return "total";
  }
  return "unknown";
}

std::optional<LatencyStage> parse_latency_stage(std::string_view name) {
  if (name == "replay") {
    return LatencyStage::Replay;
  }
  if (name == "book_update") {
    return LatencyStage::BookUpdate;
  }
  if (name == "matching") {
    return LatencyStage::Matching;
  }
  if (name == "risk") {
    return LatencyStage::Risk;
  }
  if (name == "strategy") {
    return LatencyStage::Strategy;
  }
  if (name == "inference") {
    return LatencyStage::Inference;
  }
  if (name == "total") {
    return LatencyStage::Total;
  }
  return std::nullopt;
}

std::uint64_t LatencyBudgetConfig::budget_for(LatencyStage stage) const noexcept {
  switch (stage) {
  case LatencyStage::Replay:
    return replay_ns;
  case LatencyStage::BookUpdate:
    return book_update_ns;
  case LatencyStage::Matching:
    return matching_ns;
  case LatencyStage::Risk:
    return risk_ns;
  case LatencyStage::Strategy:
    return strategy_ns;
  case LatencyStage::Inference:
    return inference_ns;
  case LatencyStage::Total:
    return total_ns;
  }
  return 0;
}

void LatencyBudgetConfig::set_budget(LatencyStage stage, std::uint64_t budget_ns) noexcept {
  switch (stage) {
  case LatencyStage::Replay:
    replay_ns = budget_ns;
    return;
  case LatencyStage::BookUpdate:
    book_update_ns = budget_ns;
    return;
  case LatencyStage::Matching:
    matching_ns = budget_ns;
    return;
  case LatencyStage::Risk:
    risk_ns = budget_ns;
    return;
  case LatencyStage::Strategy:
    strategy_ns = budget_ns;
    return;
  case LatencyStage::Inference:
    inference_ns = budget_ns;
    return;
  case LatencyStage::Total:
    total_ns = budget_ns;
    return;
  }
}

LatencyBudgetAccountant::LatencyBudgetAccountant(LatencyBudgetConfig config) : config_(config) {}

std::size_t LatencyBudgetAccountant::index_of(LatencyStage stage) noexcept {
  return static_cast<std::size_t>(static_cast<std::uint8_t>(stage) - 1U);
}

LatencyStage LatencyBudgetAccountant::stage_at(std::size_t index) noexcept {
  return kStageOrder[index];
}

void LatencyBudgetAccountant::record(LatencyStage stage, std::uint64_t duration_ns) {
  StageState& state = states_[index_of(stage)];
  ++state.sample_count;
  state.total_observed_ns += duration_ns;
  if (duration_ns > state.worst_observed_ns) {
    state.worst_observed_ns = duration_ns;
  }
}

void LatencyBudgetAccountant::reset() {
  states_ = std::array<StageState, kStageCount>{};
}

StageBudgetReport LatencyBudgetAccountant::report_for(LatencyStage stage) const {
  const StageState& state = states_[index_of(stage)];
  const std::uint64_t budget_ns = config_.budget_for(stage);

  StageBudgetReport report;
  report.stage = stage;
  report.has_budget = budget_ns > 0;
  report.budget_ns = budget_ns;
  report.sample_count = state.sample_count;
  report.worst_observed_ns = state.worst_observed_ns;
  report.total_observed_ns = state.total_observed_ns;
  report.worst_utilization_ppm = utilization_ppm(state.worst_observed_ns, budget_ns);
  report.exceeded = report.has_budget && state.sample_count > 0 &&
                    state.worst_observed_ns > budget_ns;
  return report;
}

std::vector<StageBudgetReport> LatencyBudgetAccountant::reports() const {
  std::vector<StageBudgetReport> result;
  result.reserve(kStageCount);
  for (std::size_t index = 0; index < kStageCount; ++index) {
    result.push_back(report_for(stage_at(index)));
  }
  return result;
}

std::size_t LatencyBudgetAccountant::exceeded_count() const {
  std::size_t count = 0;
  for (std::size_t index = 0; index < kStageCount; ++index) {
    if (report_for(stage_at(index)).exceeded) {
      ++count;
    }
  }
  return count;
}

std::vector<LatencyStage> LatencyBudgetAccountant::exceeded_stages() const {
  std::vector<LatencyStage> result;
  for (std::size_t index = 0; index < kStageCount; ++index) {
    const LatencyStage stage = stage_at(index);
    if (report_for(stage).exceeded) {
      result.push_back(stage);
    }
  }
  return result;
}

std::optional<LatencyStage> LatencyBudgetAccountant::worst_offender() const {
  std::optional<LatencyStage> worst;
  std::uint64_t worst_ppm = 0;
  for (std::size_t index = 0; index < kStageCount; ++index) {
    const LatencyStage stage = stage_at(index);
    const StageBudgetReport report = report_for(stage);
    if (!report.has_budget || report.sample_count == 0) {
      continue;
    }
    if (!worst.has_value() || report.worst_utilization_ppm > worst_ppm) {
      worst = stage;
      worst_ppm = report.worst_utilization_ppm;
    }
  }
  return worst;
}

std::uint64_t LatencyBudgetAccountant::config_checksum() const noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  for (std::size_t index = 0; index < kStageCount; ++index) {
    const LatencyStage stage = stage_at(index);
    seed = checksum_append(seed, stage);
    seed = checksum_append(seed, config_.budget_for(stage));
  }
  return seed;
}

std::string latency_budget_json(const LatencyBudgetAccountant& accountant) {
  std::ostringstream output;
  output << "{\n";
  output << "  \"schema_version\": 1,\n";
  output << "  \"note\": \"Observed latencies are machine-dependent; budgets are "
            "configurable and default to unset.\",\n";
  output << "  \"config_checksum\": " << accountant.config_checksum() << ",\n";

  const std::vector<StageBudgetReport> reports = accountant.reports();
  output << "  \"stages\": [\n";
  for (std::size_t i = 0; i < reports.size(); ++i) {
    const StageBudgetReport& report = reports[i];
    output << "    {\n";
    output << "      \"stage\": \"" << json_escape(to_string(report.stage)) << "\",\n";
    output << "      \"has_budget\": " << (report.has_budget ? "true" : "false") << ",\n";
    output << "      \"budget_ns\": " << report.budget_ns << ",\n";
    output << "      \"sample_count\": " << report.sample_count << ",\n";
    output << "      \"worst_observed_ns\": " << report.worst_observed_ns << ",\n";
    output << "      \"total_observed_ns\": " << report.total_observed_ns << ",\n";
    output << "      \"worst_utilization_ppm\": " << report.worst_utilization_ppm << ",\n";
    output << "      \"exceeded\": " << (report.exceeded ? "true" : "false") << "\n";
    output << "    }" << (i + 1U == reports.size() ? "\n" : ",\n");
  }
  output << "  ],\n";

  output << "  \"exceeded_count\": " << accountant.exceeded_count() << ",\n";
  const std::optional<LatencyStage> worst = accountant.worst_offender();
  output << "  \"worst_offender\": ";
  if (worst.has_value()) {
    output << "\"" << json_escape(to_string(*worst)) << "\"\n";
  } else {
    output << "null\n";
  }
  output << "}\n";
  return output.str();
}

} // namespace asterion
