#include "asterion/inference/torchscript_model.hpp"

#include <stdexcept>
#include <utility>

namespace asterion {

TorchScriptModel::TorchScriptModel(std::filesystem::path model_path)
    : model_path_(std::move(model_path)),
      load_error_("TorchScript backend is a documented placeholder; link LibTorch in a future "
                  "phase before loading external models.") {}

std::string_view TorchScriptModel::backend_name() const noexcept {
  return "torchscript-placeholder";
}

double TorchScriptModel::score(std::span<const double> /*features*/) const {
  throw std::runtime_error(load_error_);
}

} // namespace asterion
