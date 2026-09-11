#include "asterion/inference/onnx_model.hpp"

#if defined(ASTERION_HAVE_ONNXRUNTIME)

#include "asterion/inference/model_metadata.hpp"

#include <onnxruntime_cxx_api.h>

#include <cstdint>
#include <exception>
#include <stdexcept>
#include <utility>
#include <vector>

namespace asterion {

struct OnnxModel::Impl {
  Ort::Env env{ORT_LOGGING_LEVEL_WARNING, "asterion"};
  Ort::SessionOptions options{};
  Ort::Session session{nullptr};
  std::string input_name;
  std::string output_name;
  std::vector<std::int64_t> input_shape;
  std::vector<std::int64_t> output_shape;
  std::string input_shape_string;
  std::string output_shape_string;
  std::size_t input_value_count{0};

  explicit Impl(const std::filesystem::path& path) {
#if defined(_WIN32)
    session = Ort::Session(env, path.wstring().c_str(), options);
#else
    session = Ort::Session(env, path.string().c_str(), options);
#endif
    Ort::AllocatorWithDefaultOptions allocator;
    input_name = session.GetInputNameAllocated(0, allocator).get();
    output_name = session.GetOutputNameAllocated(0, allocator).get();
    input_shape = session.GetInputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    output_shape = session.GetOutputTypeInfo(0).GetTensorTypeAndShapeInfo().GetShape();
    input_shape_string = shape_to_string(input_shape);
    output_shape_string = shape_to_string(output_shape);
    input_value_count = fixed_value_count(input_shape);
  }

  [[nodiscard]] double run(std::span<const double> features) {
    if (features.size() != input_value_count) {
      throw std::invalid_argument("ONNX input feature count mismatch: expected " +
                                  std::to_string(input_value_count) + " values for input shape " +
                                  input_shape_string + ", got " +
                                  std::to_string(features.size()));
    }
    std::vector<float> input(features.begin(), features.end());
    Ort::MemoryInfo memory = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
    Ort::Value tensor = Ort::Value::CreateTensor<float>(memory, input.data(), input.size(),
                                                        input_shape.data(), input_shape.size());
    const char* input_names[] = {input_name.c_str()};
    const char* output_names[] = {output_name.c_str()};
    auto outputs =
        session.Run(Ort::RunOptions{nullptr}, input_names, &tensor, 1, output_names, 1);
    const float* data = outputs.front().GetTensorMutableData<float>();
    return static_cast<double>(data[0]);
  }

private:
  [[nodiscard]] static std::size_t fixed_value_count(std::span<const std::int64_t> shape) {
    std::size_t count = 1;
    for (const std::int64_t dim : shape) {
      if (dim <= 0) {
        throw std::runtime_error("ONNX fixture input shape must be fixed");
      }
      count *= static_cast<std::size_t>(dim);
    }
    return count;
  }
};

OnnxModel::OnnxModel(std::filesystem::path model_path)
    : model_path_(std::move(model_path)), model_name_(model_path_.filename().string()) {
  try {
    impl_ = std::make_unique<Impl>(model_path_);
    input_shape_ = impl_->input_shape_string;
    output_shape_ = impl_->output_shape_string;
    available_ = true;
  } catch (const std::exception& ex) {
    available_ = false;
    load_error_ = ex.what();
  }
}

OnnxModel::~OnnxModel() = default;

std::string_view OnnxModel::backend_name() const noexcept { return "onnx"; }

std::string_view OnnxModel::model_name() const noexcept { return model_name_; }

std::string_view OnnxModel::input_shape() const noexcept { return input_shape_; }

std::string_view OnnxModel::output_shape() const noexcept { return output_shape_; }

double OnnxModel::score(std::span<const double> features) const {
  if (!available_ || impl_ == nullptr) {
    return 0.0;
  }
  return impl_->run(features);
}

} // namespace asterion

#endif // ASTERION_HAVE_ONNXRUNTIME
