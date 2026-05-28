#include "asterion/inference/backend.hpp"

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

} // namespace

InferenceBackendSelection make_inference_backend(InferenceBackendConfig config) {
  InferenceBackendSelection selection;
  selection.requested = config.requested;

  if (config.requested == InferenceBackend::Onnx) {
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
