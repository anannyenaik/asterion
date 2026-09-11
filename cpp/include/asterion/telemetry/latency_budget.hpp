#pragma once

#include "asterion/core/types.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace asterion {

// Stages of the deterministic tick-to-trade path that can carry a latency budget.
enum class LatencyStage : std::uint8_t {
  Replay = 1,
  BookUpdate = 2,
  Matching = 3,
  Risk = 4,
  Strategy = 5,
  Inference = 6,
  Total = 7,
};

[[nodiscard]] std::string_view to_string(LatencyStage stage) noexcept;
[[nodiscard]] std::optional<LatencyStage> parse_latency_stage(std::string_view name);

// All budgets are in nanoseconds. A value of 0 means "no budget configured" for that
// stage: the stage is still measured but is never flagged as exceeded. Defaults are 0
// on purpose; there are no hard-coded latency targets because realistic budgets depend
// on the hardware and workload the operator chooses to measure.
struct LatencyBudgetConfig {
  std::uint64_t replay_ns{0};
  std::uint64_t book_update_ns{0};
  std::uint64_t matching_ns{0};
  std::uint64_t risk_ns{0};
  std::uint64_t strategy_ns{0};
  std::uint64_t inference_ns{0};
  std::uint64_t total_ns{0};

  [[nodiscard]] std::uint64_t budget_for(LatencyStage stage) const noexcept;
  void set_budget(LatencyStage stage, std::uint64_t budget_ns) noexcept;
};

struct StageBudgetReport {
  LatencyStage stage{LatencyStage::Total};
  bool has_budget{false};
  std::uint64_t budget_ns{0};
  std::size_t sample_count{0};
  std::uint64_t worst_observed_ns{0};
  std::uint64_t total_observed_ns{0};
  bool exceeded{false};
  // worst_observed_ns / budget_ns expressed in parts-per-million (integer, deterministic
  // given the same observations). Zero when no budget is configured.
  std::uint64_t worst_utilization_ppm{0};
};

class LatencyBudgetAccountant {
public:
  explicit LatencyBudgetAccountant(LatencyBudgetConfig config = {});

  void record(LatencyStage stage, std::uint64_t duration_ns);
  void reset();

  [[nodiscard]] const LatencyBudgetConfig& config() const noexcept { return config_; }
  [[nodiscard]] StageBudgetReport report_for(LatencyStage stage) const;
  [[nodiscard]] std::vector<StageBudgetReport> reports() const;
  [[nodiscard]] std::size_t exceeded_count() const;
  // Budgeted stages whose worst observation exceeded their budget, in stage order.
  [[nodiscard]] std::vector<LatencyStage> exceeded_stages() const;
  // Budgeted stage with the highest worst-case utilization, if any samples exist.
  [[nodiscard]] std::optional<LatencyStage> worst_offender() const;

  // Deterministic checksum over the configured budgets only. Measured timings are
  // machine-dependent and are deliberately excluded so the value is reproducible.
  [[nodiscard]] std::uint64_t config_checksum() const noexcept;

  static constexpr std::size_t kStageCount = 7;

private:
  struct StageState {
    std::size_t sample_count{0};
    std::uint64_t worst_observed_ns{0};
    std::uint64_t total_observed_ns{0};
  };

  [[nodiscard]] static std::size_t index_of(LatencyStage stage) noexcept;
  [[nodiscard]] static LatencyStage stage_at(std::size_t index) noexcept;

  LatencyBudgetConfig config_;
  std::array<StageState, kStageCount> states_{};
};

// Renders a stable JSON document describing the budgets and observed accounting.
[[nodiscard]] std::string latency_budget_json(const LatencyBudgetAccountant& accountant);

} // namespace asterion
