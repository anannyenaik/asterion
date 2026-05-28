#pragma once

#include "asterion/inference/model.hpp"

#include <filesystem>
#include <string>

namespace asterion {

class TorchScriptModel final : public Model {
public:
  explicit TorchScriptModel(std::filesystem::path model_path);

  [[nodiscard]] std::string_view backend_name() const noexcept override;
  [[nodiscard]] bool available() const noexcept { return false; }
  [[nodiscard]] const std::filesystem::path& model_path() const noexcept { return model_path_; }
  [[nodiscard]] const std::string& load_error() const noexcept { return load_error_; }
  [[nodiscard]] double score(std::span<const double> features) const override;

private:
  std::filesystem::path model_path_;
  std::string load_error_;
};

} // namespace asterion
