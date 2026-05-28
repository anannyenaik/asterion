#pragma once

#include "asterion/core/types.hpp"
#include "asterion/inference/model.hpp"

#include <cstdint>
#include <span>
#include <string>

namespace asterion {

enum class InferenceDecision : std::uint8_t {
  Accept = 1,
  Timeout = 2,
  LateSignal = 3,
  TimeoutAndLateSignal = 4
};

struct InferencePolicy {
  std::uint64_t timeout_ns{0};
  std::uint64_t max_signal_age_ns{0};
  bool drop_timed_out{true};
  bool drop_late_signals{true};
};

struct InferencePolicyResult {
  bool timed_out{false};
  bool late_signal{false};
  bool accepted{true};
  InferenceDecision decision{InferenceDecision::Accept};
};

struct InferenceResult {
  double score{0.0};
  std::uint64_t inference_latency_ns{0};
  bool timed_out{false};
  bool late_signal{false};
  bool accepted{true};
  InferenceDecision decision{InferenceDecision::Accept};
  std::string backend;
};

[[nodiscard]] std::string_view to_string(InferenceDecision decision) noexcept;
[[nodiscard]] InferencePolicyResult evaluate_inference_policy(
    const InferencePolicy& policy, std::uint64_t observed_latency_ns,
    TimestampNs signal_timestamp_ns = 0, TimestampNs now_timestamp_ns = 0) noexcept;
[[nodiscard]] InferenceResult measure_inference(const Model& model,
                                                std::span<const double> features,
                                                const InferencePolicy& policy = {},
                                                TimestampNs signal_timestamp_ns = 0,
                                                TimestampNs now_timestamp_ns = 0);

class MeasuredInferenceEngine {
public:
  explicit MeasuredInferenceEngine(const Model& model, InferencePolicy policy = {});

  [[nodiscard]] InferenceResult score(std::span<const double> features,
                                      TimestampNs signal_timestamp_ns = 0,
                                      TimestampNs now_timestamp_ns = 0) const;

private:
  const Model& model_;
  InferencePolicy policy_;
};

} // namespace asterion
