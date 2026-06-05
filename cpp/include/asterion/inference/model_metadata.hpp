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
  // "deterministic_fixture", "trained_recorded_public_l2". Optional; empty for
  // legacy metadata.
  std::string artefact_type;
  std::string source_repo_path;
  std::string source_commit;
  bool source_dirty{false};
  std::string export_command;
  std::vector<std::int64_t> input_shape;
  std::vector<std::int64_t> output_shape;
  // Per-timestep feature count. For windowed models the flattened input value
  // count equals feature_count * window_length.
  std::size_t feature_count{0};
  // Number of timesteps in the model input window. Optional; defaults to 1 for
  // single-timestep (legacy) artefacts. Must be >= 1.
  std::size_t window_length{1};
  std::uint32_t feature_version{0};
  // Optional content hashes recorded by the exporter. Empty for legacy metadata.
  // onnx_sha256 covers the model file; source_data_sha256 covers the recorded
  // dataset the model was trained on (for recorded-data artefacts).
  std::string onnx_sha256;
  std::string source_data_sha256;
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
