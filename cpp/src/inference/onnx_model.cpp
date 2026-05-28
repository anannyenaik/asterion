#include "asterion/inference/onnx_model.hpp"

#if defined(ASTERION_HAVE_ONNXRUNTIME)

#include <onnxruntime_cxx_api.h>

#include <array>
#include <cstdint>
#include <exception>
#include <utility>
#include <vector>

namespace asterion {

struct OnnxModel::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "asterion"};
  Ort::SessionOptions options{};
  Ort::Session session{nullptr};
  std::string input_name;
  std::string output_name;

  explicit Impl(const std::filesystem::path& path) {
#if defined(_WIN32)
    session = Ort::Session(env, path.wstring().c_str(), options);
#else
    session = Ort::Session(env, path.string().c_str(), options);
#endif
    Ort::AllocatorWithDefaultOptions allocator;
    input_name = session.GetInputNameAllocated(0, allocator).get();
    output_name = session.GetOutputNameAllocated(0, allocator).get();
  }

  [[nodiscard]] double run(std::span<const double> features) {
    std::vector<float> input(features.begin(), features.end());
    const std::array<std::int64_t, 2> shape{1, static_cast<std::int64_t>(input.size())};
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                        shape.data(), shape.size());
    const char* input_names[] = {input_name.c_str()};
    const char* output_names[] = {output_name.c_str()};
    auto outputs =
        session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
    const float* data = outputs.front().GetTensorMutableData<float>();
    return static_cast<double>(data[0]);
  }
};

OnnxModel::OnnxModel(std::filesystem::path model_path) : model_path_(std::move(model_path)) {
  try {
    impl_ = std::make_unique<Impl>(model_path_);
    available_ = true;
  } catch (const std::exception& ex) {
    available_ = false;
    load_error_ = ex.what();
  }
}

OnnxModel::~OnnxModel() = default;

std::string_view OnnxModel::backend_name() const noexcept { return "onnx"; }

double OnnxModel::score(std::span<const double> features) const {
  if (!available_ || impl_ == nullptr) {
    return 0.0;
  }
  return impl_->run(features);
}

} // namespace asterion

#endif // ASTERION_HAVE_ONNXRUNTIME
