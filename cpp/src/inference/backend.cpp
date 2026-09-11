#include "asterion/inference/backend.hpp"

#include "asterion/inference/feature_extractor.hpp"

#include <utility>

#if defined(ASTERION_HAVE_ONNXRUNTIME)
#include "asterion/inference/onnx_model.hpp"
#endif

namespace asterion {

std::string_view to_string(InferenceBackend backend) noexcept {
  switch (backend) {
  case InferenceBackend::Linear:
    return "linear";
  case InferenceBackend::Onnx:
    return "onnx";
  }
  return "unknown";
}

namespace {

[[nodiscard]] std::unique_ptr<Model> make_linear(const InferenceBackendConfig& config) {
  return std::make_unique<LinearModel>(config.linear_weights, config.linear_bias);
}

[[nodiscard]] std::string feature_contract_error(const InferenceBackendConfig& config) {
  if (config.model_feature_count > 0 && config.model_feature_count != kL2FeatureCount) {
    return "feature count mismatch: Asterion feature_count=" + std::to_string(kL2FeatureCount) +
           " model feature_count=" + std::to_string(config.model_feature_count);
  }
  if (config.model_feature_version > 0 && config.model_feature_version != kL2FeatureVersion) {
    return "feature version mismatch: Asterion feature_version=" +
           std::to_string(kL2FeatureVersion) +
           " model feature_version=" + std::to_string(config.model_feature_version);
  }
  return {};
}

} // namespace

InferenceBackendSelection make_inference_backend(InferenceBackendConfig config) {
  InferenceBackendSelection selection;
  selection.requested = config.requested;

  if (config.requested == InferenceBackend::Onnx) {
    if (const std::string error = feature_contract_error(config); !error.empty()) {
      selection.detail = error + "; using deterministic LinearModel fallback";
      selection.model = make_linear(config);
      selection.active = InferenceBackend::Linear;
      selection.fell_back = true;
      return selection;
    }
#if defined(ASTERION_HAVE_ONNXRUNTIME)
    auto onnx = std::make_unique<OnnxModel>(config.model_path);
    if (onnx->available()) {
      selection.model = std::move(onnx);
      selection.active = InferenceBackend::Onnx;
      selection.fell_back = false;
      selection.detail = "onnx runtime backend active";
      return selection;
    }
    selection.detail = "onnx runtime present but model load failed: " + onnx->load_error() +
                       "; using deterministic LinearModel fallback";
#else
    selection.detail = "onnx runtime not compiled in (configure with -DASTERION_USE_ONNXRUNTIME "
                       "and install ONNX Runtime); using deterministic LinearModel fallback";
#endif
    selection.model = make_linear(config);
    selection.active = InferenceBackend::Linear;
    selection.fell_back = true;
    return selection;
  }

  selection.model = make_linear(config);
  selection.active = InferenceBackend::Linear;
  selection.fell_back = false;
  selection.detail = "linear backend active";
  return selection;
}

} // namespace asterion
