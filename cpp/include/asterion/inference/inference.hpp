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
  // When > 0 and disable_on_repeated_late_signals is set, the stateful
  // InferencePolicyGate disables the model after this many consecutive late
  // signals. 0 (the default) keeps the model enabled regardless of late count.
  std::uint32_t max_consecutive_late_signals{0};
  bool disable_on_repeated_late_signals{false};
};

struct InferencePolicyResult {
  bool timed_out{false};
  bool late_signal{false};
  bool accepted{true};
  InferenceDecision decision{InferenceDecision::Accept};
  // Set by the stateful InferencePolicyGate once the model has been disabled
  // after repeated late signals. The stateless evaluate_inference_policy never
  // sets this field.
  bool model_disabled{false};
};

struct InferenceResult {
  double score{0.0};
  std::uint64_t inference_latency_ns{0};
  bool timed_out{false};
  bool late_signal{false};
  bool accepted{true};
  InferenceDecision decision{InferenceDecision::Accept};
  std::string backend;
  std::string model_name;
  std::string input_shape;
  std::string output_shape;
  bool model_disabled{false};
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

// Stateful late-signal guard. Tracks consecutive late signals across calls and,
// when configured, disables the model after a repeated-late threshold is crossed.
// It is deliberately driven by injected (latency, signal_ts, now_ts) values so it
// can be tested deterministically without wall-clock timing. Once disabled it
// abstains (accepted == false, model_disabled == true) until reset().
class InferencePolicyGate {
public:
  explicit InferencePolicyGate(InferencePolicy policy) noexcept;

  // Evaluate one observation. Updates the consecutive late-signal counter and the
  // disabled latch, then returns the policy result reflecting both the per-call
  // decision and any latched disable.
  [[nodiscard]] InferencePolicyResult observe(std::uint64_t observed_latency_ns,
                                              TimestampNs signal_timestamp_ns = 0,
                                              TimestampNs now_timestamp_ns = 0) noexcept;

  [[nodiscard]] bool model_disabled() const noexcept { return disabled_; }
  [[nodiscard]] std::uint32_t consecutive_late_signals() const noexcept {
    return consecutive_late_signals_;
  }
  void reset() noexcept;

private:
  InferencePolicy policy_;
  std::uint32_t consecutive_late_signals_{0};
  bool disabled_{false};
};

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
