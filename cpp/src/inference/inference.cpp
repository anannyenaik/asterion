#include "asterion/inference/inference.hpp"

#include <chrono>
#include <string_view>

namespace asterion {

std::string_view to_string(InferenceDecision decision) noexcept {
  switch (decision) {
  case InferenceDecision::Accept:
    return "accept";
  case InferenceDecision::Timeout:
    return "timeout";
  case InferenceDecision::LateSignal:
    return "late_signal";
  case InferenceDecision::TimeoutAndLateSignal:
    return "timeout_and_late_signal";
  }
  return "unknown";
}

InferencePolicyResult evaluate_inference_policy(const InferencePolicy& policy,
                                                std::uint64_t observed_latency_ns,
                                                TimestampNs signal_timestamp_ns,
                                                TimestampNs now_timestamp_ns) noexcept {
  InferencePolicyResult result;
  result.timed_out = policy.timeout_ns > 0 && observed_latency_ns > policy.timeout_ns;

  if (policy.max_signal_age_ns > 0 && signal_timestamp_ns > 0 &&
      now_timestamp_ns >= signal_timestamp_ns) {
    const auto age_ns = static_cast<std::uint64_t>(now_timestamp_ns - signal_timestamp_ns);
    result.late_signal = age_ns > policy.max_signal_age_ns;
  }

  result.accepted = !(result.timed_out && policy.drop_timed_out) &&
                    !(result.late_signal && policy.drop_late_signals);

  if (result.timed_out && result.late_signal) {
    result.decision = InferenceDecision::TimeoutAndLateSignal;
  } else if (result.timed_out) {
    result.decision = InferenceDecision::Timeout;
  } else if (result.late_signal) {
    result.decision = InferenceDecision::LateSignal;
  } else {
    result.decision = InferenceDecision::Accept;
  }

  return result;
}

InferenceResult measure_inference(const Model& model, std::span<const double> features,
                                  const InferencePolicy& policy,
                                  TimestampNs signal_timestamp_ns,
                                  TimestampNs now_timestamp_ns) {
  const auto start = std::chrono::steady_clock::now();
  const double score = model.score(features);
  const auto end = std::chrono::steady_clock::now();
  const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
  std::uint64_t latency_ns = 0;
  if (elapsed > 0) {
    latency_ns = static_cast<std::uint64_t>(elapsed);
  }
  const InferencePolicyResult policy_result =
      evaluate_inference_policy(policy, latency_ns, signal_timestamp_ns, now_timestamp_ns);

  return InferenceResult{score,
                         latency_ns,
                         policy_result.timed_out,
                         policy_result.late_signal,
                         policy_result.accepted,
                         policy_result.decision,
                         std::string(model.backend_name())};
}

MeasuredInferenceEngine::MeasuredInferenceEngine(const Model& model, InferencePolicy policy)
    : model_(model), policy_(policy) {}

InferenceResult MeasuredInferenceEngine::score(std::span<const double> features,
                                               TimestampNs signal_timestamp_ns,
                                               TimestampNs now_timestamp_ns) const {
  return measure_inference(model_, features, policy_, signal_timestamp_ns, now_timestamp_ns);
}

} // namespace asterion
