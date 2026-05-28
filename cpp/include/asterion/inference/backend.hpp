#pragma once

#include "asterion/inference/linear_model.hpp"
#include "asterion/inference/model.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace asterion {

enum class InferenceBackend : std::uint8_t { Linear = 1, Onnx = 2 };

// True only when the project was built with a working ONNX Runtime dependency
// (-DASTERION_USE_ONNXRUNTIME and the library found at configure time). When false,
// requesting the ONNX backend deterministically falls back to LinearModel. This is
// a compile-time constant so tests can branch on the build configuration.
#if defined(ASTERION_HAVE_ONNXRUNTIME)
inline constexpr bool kOnnxRuntimeAvailable = true;
#else
inline constexpr bool kOnnxRuntimeAvailable = false;
#endif

struct InferenceBackendConfig {
  InferenceBackend requested{InferenceBackend::Linear};
  // Used by the ONNX backend; ignored by the LinearModel.
  std::filesystem::path model_path{};
  // Weights/bias for the LinearModel, used directly for the Linear backend and as
  // the deterministic fallback whenever the ONNX backend is unavailable.
  std::vector<double> linear_weights{};
  double linear_bias{0.0};
};

struct InferenceBackendSelection {
  std::unique_ptr<Model> model;
  InferenceBackend requested{InferenceBackend::Linear};
  InferenceBackend active{InferenceBackend::Linear};
  bool fell_back{false};
  std::string detail;
};

[[nodiscard]] std::string_view to_string(InferenceBackend backend) noexcept;

// Build the requested inference backend. The returned selection always owns a
// usable Model: an ONNX request degrades to a LinearModel (fell_back == true) when
// ONNX Runtime is not compiled in or the model cannot be loaded.
[[nodiscard]] InferenceBackendSelection make_inference_backend(InferenceBackendConfig config);

} // namespace asterion
