#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <vector>

namespace asterion {

struct ModelMetadata {
  std::string model_name;
  std::string model_class;
  // One of: "trained_synthetic_smoke", "exported_untrained_architecture",
  // "deterministic_fixture". Optional; empty for legacy metadata.
  std::string artefact_type;
  std::string source_repo_path;
  std::string source_commit;
  bool source_dirty{false};
  std::string export_command;
  std::vector<std::int64_t> input_shape;
  std::vector<std::int64_t> output_shape;
  std::size_t feature_count{0};
  std::uint32_t feature_version{0};
  std::vector<double> expected_test_input;
  std::vector<double> expected_test_output;
  // Optional deterministic linear head. Present for the hand-written fixture and
  // absent for real exported models (whose behaviour lives in the ONNX graph).
  std::vector<double> reference_weights;
  double reference_bias{0.0};
  bool trained_model{false};
  bool deterministic_fixture{false};
  std::string claim_boundary;
};

// Number of scalar input values implied by a shape (product of dims). Returns 0
// when the shape is empty or contains a non-positive (dynamic) dimension.
[[nodiscard]] std::size_t shape_value_count(std::span<const std::int64_t> shape) noexcept;

struct ModelMetadataValidation {
  bool ok{false};
  std::string error;
};

[[nodiscard]] std::string shape_to_string(std::span<const std::int64_t> shape);
[[nodiscard]] ModelMetadata load_model_metadata(const std::filesystem::path& path);
[[nodiscard]] ModelMetadataValidation validate_model_metadata(const ModelMetadata& metadata);
[[nodiscard]] ModelMetadataValidation validate_feature_compatibility(
    const ModelMetadata& metadata, std::size_t feature_count, std::uint32_t feature_version);
[[nodiscard]] double score_reference_fixture(const ModelMetadata& metadata,
                                             std::span<const double> features);

} // namespace asterion
