#pragma once

#include "asterion/inference/model.hpp"

#include <filesystem>
#include <string>

#if defined(ASTERION_HAVE_ONNXRUNTIME)
#include <memory>

namespace asterion {

// Real ONNX Runtime inference backend. Only compiled when the project is built
// with the optional ONNX Runtime dependency (-DASTERION_USE_ONNXRUNTIME). It is
// not exercised by CI; the deterministic LinearModel is the default and fallback.
class OnnxModel final : public Model {
public:
  explicit OnnxModel(std::filesystem::path model_path);
  ~OnnxModel() override;

  [[nodiscard]] std::string_view backend_name() const noexcept override;
  [[nodiscard]] bool available() const noexcept { return available_; }
  [[nodiscard]] const std::string& load_error() const noexcept { return load_error_; }
  [[nodiscard]] const std::filesystem::path& model_path() const noexcept { return model_path_; }
  [[nodiscard]] double score(std::span<const double> features) const override;

private:
  struct Impl;
  std::filesystem::path model_path_;
  bool available_{false};
  std::string load_error_;
  std::unique_ptr<Impl> impl_;
};

} // namespace asterion

#endif // ASTERION_HAVE_ONNXRUNTIME
