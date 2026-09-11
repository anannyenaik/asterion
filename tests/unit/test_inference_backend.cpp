#include "asterion/book/order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/inference/backend.hpp"
#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"
#include "asterion/inference/model_metadata.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <exception>
#include <filesystem>
#include <fstream>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

using namespace asterion;

namespace {

std::filesystem::path chronoslob_model_path() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models" /
         "chronoslob_tiny_fixture.onnx";
}

std::filesystem::path chronoslob_metadata_path() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models" /
         "chronoslob_tiny_fixture.metadata.json";
}

std::filesystem::path chronoslob_synthetic_model_path() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models" /
         "chronoslob_tiny_synthetic.onnx";
}

std::filesystem::path chronoslob_synthetic_metadata_path() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models" /
         "chronoslob_tiny_synthetic.metadata.json";
}

std::filesystem::path chronoslob_public_l2_model_path() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models" /
         "chronoslob_public_l2_tiny.onnx";
}

std::filesystem::path chronoslob_public_l2_metadata_path() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models" /
         "chronoslob_public_l2_tiny.metadata.json";
}

class TemporaryMetadataFile {
public:
  TemporaryMetadataFile(std::string_view name, std::string_view contents)
      : path_(std::filesystem::temp_directory_path() /
              ("asterion_" + std::string(name) + ".metadata.json")) {
    std::ofstream output(path_);
    if (!output) {
      throw std::runtime_error("unable to create temporary metadata file");
    }
    output << contents;
  }

  ~TemporaryMetadataFile() {
    std::error_code error;
    std::filesystem::remove(path_, error);
  }

  TemporaryMetadataFile(const TemporaryMetadataFile&) = delete;
  TemporaryMetadataFile& operator=(const TemporaryMetadataFile&) = delete;

  [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

private:
  std::filesystem::path path_;
};

void require_metadata_load_failure(const std::filesystem::path& path, std::string_view expected) {
  try {
    (void)load_model_metadata(path);
    FAIL("expected metadata load to fail");
  } catch (const std::exception& exception) {
    REQUIRE(std::string(exception.what()).find(expected) != std::string::npos);
  }
}

} // namespace

TEST_CASE("ChronosLOB tiny fixture metadata loads and validates",
          "[inference][backend][metadata]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_metadata_path());
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE(validation.ok);
  REQUIRE(validation.error.empty());
  REQUIRE(metadata.model_name == "chronoslob_tiny_fixture");
  REQUIRE(metadata.trained_model == false);
  REQUIRE(metadata.deterministic_fixture == true);
  REQUIRE(shape_to_string(metadata.input_shape) == "1x4");
  REQUIRE(shape_to_string(metadata.output_shape) == "1x1");
  REQUIRE(metadata.feature_count == kL2FeatureCount);
  REQUIRE(metadata.feature_version == kL2FeatureVersion);
  REQUIRE(validate_feature_compatibility(metadata, kL2FeatureCount, kL2FeatureVersion).ok);
}

TEST_CASE("ChronosLOB tiny fixture expected input produces expected output",
          "[inference][backend][metadata]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_metadata_path());
  REQUIRE(validate_model_metadata(metadata).ok);

  const double score = score_reference_fixture(metadata, metadata.expected_test_input);

  REQUIRE(metadata.expected_test_output.size() == 1);
  REQUIRE(score == Catch::Approx(metadata.expected_test_output.front()));
}

TEST_CASE("ChronosLOB fixture feature contract mismatches fail clearly",
          "[inference][backend][metadata]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_metadata_path());

  metadata.feature_count = kL2FeatureCount + 1U;
  ModelMetadataValidation count_validation =
      validate_feature_compatibility(metadata, kL2FeatureCount, kL2FeatureVersion);
  REQUIRE_FALSE(count_validation.ok);
  REQUIRE(count_validation.error.find("feature count mismatch") != std::string::npos);

  metadata = load_model_metadata(chronoslob_metadata_path());
  metadata.feature_version = kL2FeatureVersion + 1U;
  ModelMetadataValidation version_validation =
      validate_feature_compatibility(metadata, kL2FeatureCount, kL2FeatureVersion);
  REQUIRE_FALSE(version_validation.ok);
  REQUIRE(version_validation.error.find("feature version mismatch") != std::string::npos);
}

TEST_CASE("Trained synthetic artefact metadata loads and validates",
          "[inference][backend][metadata][synthetic]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_synthetic_metadata_path());
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE(validation.ok);
  REQUIRE(validation.error.empty());
  REQUIRE(metadata.model_name == "chronoslob_tiny_synthetic");
  REQUIRE(metadata.model_class == "DeepLOBModel");
  REQUIRE(metadata.artefact_type == "trained_synthetic_smoke");
  REQUIRE(metadata.trained_model == true);
  REQUIRE(metadata.deterministic_fixture == false);
  REQUIRE(shape_to_string(metadata.input_shape) == "1x1x4");
  REQUIRE(shape_to_string(metadata.output_shape) == "1x3");
  REQUIRE(metadata.feature_count == kL2FeatureCount);
  REQUIRE(metadata.feature_version == kL2FeatureVersion);
  // A trained artefact carries no hand-written linear head.
  REQUIRE(metadata.reference_weights.empty());
  // The 4 caller-owned L2 features feed the [1, 1, 4] single-timestep input.
  REQUIRE(shape_value_count(metadata.input_shape) == kL2FeatureCount);
  REQUIRE(metadata.expected_test_input.size() == kL2FeatureCount);
  REQUIRE(metadata.expected_test_output.size() == 3);
  REQUIRE(validate_feature_compatibility(metadata, kL2FeatureCount, kL2FeatureVersion).ok);
}

TEST_CASE("Model metadata rejects an input shape that does not match feature_count",
          "[inference][backend][metadata][synthetic]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_synthetic_metadata_path());
  metadata.input_shape = {1, 1, 8}; // 8 values but feature_count is 4
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("unsupported model shape") != std::string::npos);
}

TEST_CASE("Model metadata rejects a trained_model/artefact_type mismatch",
          "[inference][backend][metadata][synthetic]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_synthetic_metadata_path());
  REQUIRE(metadata.artefact_type == "trained_synthetic_smoke");
  metadata.trained_model = false; // inconsistent with a trained artefact_type
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("artefact_type/trained_model mismatch") != std::string::npos);
}

TEST_CASE("Model metadata rejects an artefact_type outside the supported vocabulary",
          "[inference][backend][metadata][synthetic]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_synthetic_metadata_path());
  metadata.artefact_type = "not_a_supported_artefact_type";
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("unsupported artefact_type") != std::string::npos);
}

TEST_CASE("Every accepted model artefact type is represented by shipped metadata",
          "[inference][backend][metadata]") {
  const std::filesystem::path models =
      std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models";
  std::set<std::string> shipped_types;
  for (const std::filesystem::directory_entry& entry : std::filesystem::directory_iterator(models)) {
    const std::string filename = entry.path().filename().string();
    if (!entry.is_regular_file() || !filename.ends_with(".metadata.json")) {
      continue;
    }
    const ModelMetadata metadata = load_model_metadata(entry.path());
    if (!metadata.artefact_type.empty()) {
      REQUIRE(validate_model_metadata(metadata).ok);
      shipped_types.insert(metadata.artefact_type);
    }
  }

  const std::set<std::string> accepted_types(kSupportedModelArtefactTypes.begin(),
                                             kSupportedModelArtefactTypes.end());
  REQUIRE(shipped_types == accepted_types);
}

TEST_CASE("ChronosLOB public-L2 windowed artefact metadata loads and validates",
          "[inference][backend][metadata][public_l2]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE(validation.ok);
  REQUIRE(validation.error.empty());
  REQUIRE(metadata.model_name == "chronoslob_public_l2_tiny");
  REQUIRE(metadata.model_class == "DeepLOBModel");
  REQUIRE(metadata.artefact_type == "trained_recorded_public_l2");
  REQUIRE(metadata.trained_model == true);
  REQUIRE(metadata.deterministic_fixture == false);
  // Multi-timestep window contract: input is [1, 16, 40], output [1, 3].
  REQUIRE(metadata.window_length == 16U);
  REQUIRE(metadata.feature_count == 40U);
  REQUIRE(shape_to_string(metadata.input_shape) == "1x16x40");
  REQUIRE(shape_to_string(metadata.output_shape) == "1x3");
  // Flattened input value count is feature_count * window_length.
  REQUIRE(shape_value_count(metadata.input_shape) == metadata.feature_count * metadata.window_length);
  REQUIRE(metadata.expected_test_input.size() == metadata.feature_count * metadata.window_length);
  REQUIRE(metadata.expected_test_output.size() == 3);
  // A trained artefact carries no hand-written linear head.
  REQUIRE(metadata.reference_weights.empty());
  // Content hashes are recorded by the exporter.
  REQUIRE(metadata.onnx_sha256.size() == 64);
  REQUIRE(metadata.source_data_sha256.size() == 64);
}

TEST_CASE("Public-L2 windowed model is not the event-loop 4-feature contract",
          "[inference][backend][metadata][public_l2]") {
  // The windowed artefact has a richer (40-feature x 16-step) contract than the
  // caller-owned L2 feature buffer, so it must not be accepted as the event-loop
  // model: feature compatibility fails clearly.
  const ModelMetadata metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  const ModelMetadataValidation compat =
      validate_feature_compatibility(metadata, kL2FeatureCount, kL2FeatureVersion);
  REQUIRE_FALSE(compat.ok);
  REQUIRE(compat.error.find("feature count mismatch") != std::string::npos);

  // An ONNX request that declares the windowed model's feature count against the
  // caller-owned feature buffer falls back to the deterministic LinearModel before
  // any model load, so the windowed artefact cannot stand in for the event-loop model.
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_public_l2_model_path();
  config.model_feature_count = metadata.feature_count; // 40 != kL2FeatureCount (4)
  config.model_feature_version = metadata.feature_version;
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;
  const InferenceBackendSelection selection = make_inference_backend(config);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Linear);
  REQUIRE(selection.fell_back);
  REQUIRE(selection.detail.find("feature count mismatch") != std::string::npos);
  const std::array<double, 4> features{5.0, 0.0, 0.0, 0.0};
  REQUIRE(selection.model->score(features) == Catch::Approx(5.0));
}

TEST_CASE("Public-L2 isolated ONNX benchmark config selects ONNX or is detectably skipped",
          "[inference][backend][onnx][public_l2]") {
  // Mirrors the config used by the optional isolated C++ ONNX benchmark row
  // (public_l2_chronoslob_onnx_inference_only): the standalone windowed artefact
  // is loaded directly with model_feature_count left unset so the event-loop
  // 4-feature buffer gate is skipped. The benchmark requires active == Onnx (no fallback)
  // before timing, so this is the invariant that decides whether the row is
  // measured or reported skipped. It is asserted in BOTH build configurations.
  const ModelMetadata metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  REQUIRE(validate_model_metadata(metadata).ok);

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_public_l2_model_path();
  // model_feature_count deliberately unset: standalone windowed model contract.
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.requested == InferenceBackend::Onnx);

  if (kOnnxRuntimeAvailable) {
    // ONNX present: the benchmark times genuine ONNX scoring of the [1,16,40]
    // artefact and reproduces the recorded expected output before timing.
    REQUIRE(selection.active == InferenceBackend::Onnx);
    REQUIRE_FALSE(selection.fell_back);
    REQUIRE(selection.model->backend_name() == "onnx");
    REQUIRE(selection.model->input_shape() == "1x16x40");
    REQUIRE(selection.model->output_shape() == "1x3");
    const double score = selection.model->score(metadata.expected_test_input);
    REQUIRE(score == Catch::Approx(metadata.expected_test_output.front()).margin(1e-3));
  } else {
    // ONNX absent: selection falls back to the LinearModel and sets fell_back.
    // That flag is what makes the benchmark report the row as skipped instead of
    // timing the LinearModel under an ONNX-named row.
    REQUIRE(selection.active == InferenceBackend::Linear);
    REQUIRE(selection.fell_back);
    REQUIRE(selection.model->backend_name() == "linear");
  }
}

TEST_CASE("Windowed metadata shape validation respects window_length",
          "[inference][backend][metadata][public_l2]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  // Breaking the window_length so feature_count * window_length no longer equals
  // the flattened input value count must be rejected as an unsupported shape.
  metadata.window_length = 8; // 40 * 8 = 320 != 640
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("unsupported model shape") != std::string::npos);
}

TEST_CASE("Public-L2 metadata enforces its windowed input and batched output shapes",
          "[inference][backend][metadata][public_l2]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  metadata.input_shape = {1, 8, 80}; // same value count, wrong [batch, window, features] layout
  ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("unsupported recorded-public-L2 input_shape") != std::string::npos);

  metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  metadata.output_shape = {3}; // same value count, missing the public-L2 batch dimension
  validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("fixed batch dimension") != std::string::npos);
}

TEST_CASE("Public-L2 metadata rejects zero windows and wrong expected-input lengths",
          "[inference][backend][metadata][public_l2]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  metadata.window_length = 0;
  ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("window_length must be positive") != std::string::npos);

  metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  metadata.expected_test_input.pop_back();
  validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("expected_test_input size") != std::string::npos);
}

TEST_CASE("Metadata numeric-array parser fails cleanly on malformed, truncated and missing arrays",
          "[inference][backend][metadata][parser]") {
  constexpr std::string_view prefix = R"({
    "model_name": "parser_test",
    "input_shape": [1, 2],
    "output_shape": [1, 1],
    "feature_count": 2,
    "feature_version": 1
  )";
  constexpr std::string_view suffix = R"(,
    "expected_test_output": [0.0],
    "trained_model": false
  })";

  SECTION("malformed token") {
    const TemporaryMetadataFile file(
        "malformed_array",
        std::string(prefix) + R"(, "expected_test_input": [1.0, nope])" + std::string(suffix));
    require_metadata_load_failure(file.path(), "invalid number: expected_test_input");
  }

  SECTION("truncated array") {
    const TemporaryMetadataFile file(
        "truncated_array",
        std::string(prefix) + R"(, "expected_test_input": [1.0, 2.0)");
    require_metadata_load_failure(file.path(), "truncated numeric array: expected_test_input");
  }

  SECTION("missing required array") {
    const TemporaryMetadataFile file("missing_array", std::string(prefix) + std::string(suffix));
    require_metadata_load_failure(file.path(), "missing field: expected_test_input");
  }
}

TEST_CASE("ONNX request for the trained artefact with a mismatched feature count falls back clearly",
          "[inference][backend][fallback][synthetic]") {
  // Deterministic regardless of whether ONNX Runtime is compiled in: the feature
  // contract is checked before any model load is attempted.
  const ModelMetadata metadata = load_model_metadata(chronoslob_synthetic_metadata_path());
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_synthetic_model_path();
  config.model_feature_count = metadata.feature_count + 1U;
  config.model_feature_version = metadata.feature_version;
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Linear);
  REQUIRE(selection.fell_back);
  REQUIRE(selection.detail.find("feature count mismatch") != std::string::npos);
  const std::array<double, 4> features{3.0, 0.0, 0.0, 0.0};
  REQUIRE(selection.model->score(features) == Catch::Approx(3.0));
}

TEST_CASE("ONNX request with incompatible metadata falls back with a clear diagnostic",
          "[inference][backend][fallback]") {
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_model_path();
  config.model_feature_count = kL2FeatureCount + 1U;
  config.model_feature_version = kL2FeatureVersion;
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;

  const InferenceBackendSelection selection = make_inference_backend(config);

  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Linear);
  REQUIRE(selection.fell_back);
  REQUIRE(selection.detail.find("feature count mismatch") != std::string::npos);
  const std::array<double, 4> features{2.0, 0.0, 0.0, 0.0};
  REQUIRE(selection.model->score(features) == Catch::Approx(2.0));
}

TEST_CASE("Linear backend is selected and scores deterministically", "[inference][backend]") {
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Linear;
  config.linear_weights = {0.5, 0.0, 2.0, 0.001};
  config.linear_bias = 1.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Linear);
  REQUIRE_FALSE(selection.fell_back);
  REQUIRE(to_string(selection.active) == "linear");

  const std::array<double, 4> features{2.0, 1000.0, 0.5, 400.0};
  REQUIRE(selection.model->score(features) == Catch::Approx(3.4));
  REQUIRE(selection.model->backend_name() == "linear");
}

TEST_CASE("ONNX request falls back to LinearModel when ONNX Runtime is absent",
          "[inference][backend][fallback]") {
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "fixtures" /
                      "missing_identity.onnx";
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.requested == InferenceBackend::Onnx);
  REQUIRE_FALSE(selection.detail.empty());
  REQUIRE(selection.active == InferenceBackend::Linear);
  REQUIRE(selection.fell_back);
  REQUIRE(selection.model->backend_name() == "linear");
  const std::array<double, 4> features{2.0, 0.0, 0.0, 0.0};
  REQUIRE(selection.model->score(features) == Catch::Approx(2.0));
}

#if defined(ASTERION_HAVE_ONNXRUNTIME)
TEST_CASE("ONNX Runtime fixture backend scores deterministically when opt-in dependency is present",
          "[inference][backend][onnx]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_metadata_path());

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_model_path();
  config.model_name = metadata.model_name;
  config.input_shape = shape_to_string(metadata.input_shape);
  config.output_shape = shape_to_string(metadata.output_shape);
  config.model_feature_count = metadata.feature_count;
  config.model_feature_version = metadata.feature_version;
  config.linear_weights = {99.0, 99.0, 99.0, 99.0};
  config.linear_bias = 99.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  INFO(selection.detail);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.requested == InferenceBackend::Onnx);
  REQUIRE(selection.active == InferenceBackend::Onnx);
  REQUIRE_FALSE(selection.fell_back);
  REQUIRE(selection.model->backend_name() == "onnx");
  REQUIRE(selection.model->input_shape() == "1x4");
  REQUIRE(selection.model->output_shape() == "1x1");

  const std::vector<double> features = metadata.expected_test_input;
  const double first_score = selection.model->score(features);
  const double second_score = selection.model->score(features);
  REQUIRE(first_score == Catch::Approx(metadata.expected_test_output.front()));
  REQUIRE(first_score == Catch::Approx(second_score));

  const std::array<double, 3> bad_features{1.0, 2.0, 3.0};
  try {
    (void)selection.model->score(bad_features);
    FAIL("expected ONNX input feature count mismatch");
  } catch (const std::exception& ex) {
    REQUIRE(std::string(ex.what()).find("ONNX input feature count mismatch") !=
            std::string::npos);
  }

  MeasuredInferenceEngine engine(*selection.model, InferencePolicy{1'000'000'000, 0, true, true});
  const InferenceResult result = engine.score(features);
  REQUIRE(result.backend == "onnx");
  REQUIRE(result.model_name == "chronoslob_tiny_fixture.onnx");
  REQUIRE(result.input_shape == "1x4");
  REQUIRE(result.output_shape == "1x1");
  REQUIRE(result.accepted);
  REQUIRE(result.decision == InferenceDecision::Accept);
  REQUIRE(result.score == Catch::Approx(metadata.expected_test_output.front()));
}

TEST_CASE("ONNX inference allocations are separated from load",
          "[inference][backend][onnx][alloc]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_metadata_path());

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_model_path();
  config.model_name = metadata.model_name;
  config.input_shape = shape_to_string(metadata.input_shape);
  config.output_shape = shape_to_string(metadata.output_shape);
  config.model_feature_count = metadata.feature_count;
  config.model_feature_version = metadata.feature_version;
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;

  // Model-load allocations (session/graph setup) are expected and are measured
  // separately from steady-state inference; we do not require them to be zero.
  reset_allocation_counters();
  const InferenceBackendSelection selection = make_inference_backend(config);
  const AllocationSnapshot load_snapshot = allocation_snapshot();
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Onnx);

  const std::vector<double> features = metadata.expected_test_input;

  // Warm up once so any lazy first-call allocations are not counted as steady state.
  const double warm = selection.model->score(features);
  REQUIRE(warm == Catch::Approx(metadata.expected_test_output.front()));

  // Steady-state inference allocations: measured and reported, not asserted to be
  // zero. ONNX Runtime may allocate per-run buffers.
  reset_allocation_counters();
  double last = 0.0;
  for (int i = 0; i < 16; ++i) {
    last = selection.model->score(features);
  }
  const AllocationSnapshot steady_snapshot = allocation_snapshot();

  INFO("onnx load allocations=" << load_snapshot.allocations
                                << " bytes=" << load_snapshot.bytes_allocated
                                << "; steady-state allocations=" << steady_snapshot.allocations
                                << " over 16 scores");
  REQUIRE(last == Catch::Approx(metadata.expected_test_output.front())); // deterministic scoring
  // The allocation counts above are informational and reported via INFO; we do
  // not assert ONNX inference is allocation-free because that is not proven.
  // Determinism is the invariant being tested here.
  (void)steady_snapshot;
  (void)load_snapshot;

}

TEST_CASE("ONNX Runtime loads the trained synthetic DeepLOB artefact and scores deterministically",
          "[inference][backend][onnx][synthetic]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_synthetic_metadata_path());
  REQUIRE(validate_model_metadata(metadata).ok);
  REQUIRE(metadata.trained_model);

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_synthetic_model_path();
  config.model_name = metadata.model_name;
  config.input_shape = shape_to_string(metadata.input_shape);
  config.output_shape = shape_to_string(metadata.output_shape);
  config.model_feature_count = metadata.feature_count;
  config.model_feature_version = metadata.feature_version;
  config.linear_weights = {99.0, 99.0, 99.0, 99.0};
  config.linear_bias = 99.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  INFO(selection.detail);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Onnx);
  REQUIRE_FALSE(selection.fell_back);
  REQUIRE(selection.model->backend_name() == "onnx");
  REQUIRE(selection.model->input_shape() == "1x1x4");
  REQUIRE(selection.model->output_shape() == "1x3");

  // The 4 caller-owned L2 features feed the [1, 1, 4] input directly; score() is
  // the first output logit. Determinism (artefact -> output) is the invariant.
  const std::vector<double> features = metadata.expected_test_input;
  const double first_score = selection.model->score(features);
  const double second_score = selection.model->score(features);
  REQUIRE(first_score == Catch::Approx(metadata.expected_test_output.front()).margin(1e-3));
  REQUIRE(first_score == Catch::Approx(second_score));

  MeasuredInferenceEngine engine(*selection.model, InferencePolicy{1'000'000'000, 0, true, true});
  const InferenceResult result = engine.score(features);
  REQUIRE(result.backend == "onnx");
  REQUIRE(result.model_name == "chronoslob_tiny_synthetic.onnx");
  REQUIRE(result.input_shape == "1x1x4");
  REQUIRE(result.output_shape == "1x3");
  REQUIRE(result.accepted);
  REQUIRE(result.decision == InferenceDecision::Accept);
  REQUIRE(result.score == Catch::Approx(metadata.expected_test_output.front()).margin(1e-3));
}

TEST_CASE("Trained synthetic ONNX load allocations are separated from steady-state inference",
          "[inference][backend][onnx][alloc][synthetic]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_synthetic_metadata_path());

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_synthetic_model_path();
  config.model_feature_count = metadata.feature_count;
  config.model_feature_version = metadata.feature_version;
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;

  reset_allocation_counters();
  const InferenceBackendSelection selection = make_inference_backend(config);
  const AllocationSnapshot load_snapshot = allocation_snapshot();
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Onnx);

  const std::vector<double> features = metadata.expected_test_input;
  const double warm = selection.model->score(features);
  REQUIRE(warm == Catch::Approx(metadata.expected_test_output.front()).margin(1e-3));

  reset_allocation_counters();
  double last = 0.0;
  for (int i = 0; i < 16; ++i) {
    last = selection.model->score(features);
  }
  const AllocationSnapshot steady_snapshot = allocation_snapshot();
  INFO("trained-artefact onnx load allocations=" << load_snapshot.allocations << " bytes="
       << load_snapshot.bytes_allocated << "; steady-state allocations="
       << steady_snapshot.allocations << " over 16 scores");
  REQUIRE(last == Catch::Approx(metadata.expected_test_output.front()).margin(1e-3));
  (void)steady_snapshot;
  (void)load_snapshot;
}

TEST_CASE("ONNX Runtime loads the public-L2 windowed artefact and reproduces its fixture",
          "[inference][backend][onnx][public_l2]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_public_l2_metadata_path());
  REQUIRE(validate_model_metadata(metadata).ok);
  REQUIRE(metadata.trained_model);

  // The windowed artefact stands alone rather than feeding the event-loop
  // 4-feature buffer, so it is loaded directly: model_feature_count is left unset
  // and the feature-buffer compatibility gate is skipped. A load failure would fall
  // back to LinearModel and fail the REQUIRE on active == Onnx below.
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_public_l2_model_path();
  config.linear_weights = {99.0, 99.0, 99.0, 99.0};
  config.linear_bias = 99.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  INFO(selection.detail);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Onnx);
  REQUIRE_FALSE(selection.fell_back);
  REQUIRE(selection.model->backend_name() == "onnx");
  REQUIRE(selection.model->input_shape() == "1x16x40");
  REQUIRE(selection.model->output_shape() == "1x3");

  // Feed the recorded expected-input window (640 flattened values) and reproduce
  // the recorded expected output deterministically.
  const std::vector<double> features = metadata.expected_test_input;
  const double first_score = selection.model->score(features);
  const double second_score = selection.model->score(features);
  REQUIRE(first_score == Catch::Approx(metadata.expected_test_output.front()).margin(1e-3));
  REQUIRE(first_score == Catch::Approx(second_score));
}
#endif

TEST_CASE("Selected backend integrates with feature extraction and latency accounting",
          "[inference][backend]") {
  OrderBook book(7);
  REQUIRE(book.add_order(Order{1, 11, 7, Side::Buy, 999, 300, 1, 1}));
  REQUIRE(book.add_order(Order{2, 12, 7, Side::Sell, 1001, 100, 2, 2}));

  FeatureExtractor extractor;
  L2View view;
  view.reserve(1);
  book.fill_l2_view(1, view);
  std::array<double, kL2FeatureCount> feature_storage{};
  FeatureBuffer feature_buffer{feature_storage};
  REQUIRE(extractor.extract_into(view, feature_buffer) == FeatureExtractionStatus::Ok);
  const std::span<const double> features = feature_buffer.used();

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Linear;
  config.linear_weights = {0.5, 0.0, 2.0, 0.001};
  config.linear_bias = 1.0;
  const InferenceBackendSelection selection = make_inference_backend(config);

  const LinearModel reference(config.linear_weights, config.linear_bias);
  MeasuredInferenceEngine engine(*selection.model,
                                 InferencePolicy{1'000'000'000, 0, true, true});
  const InferenceResult result = engine.score(features);

  REQUIRE(result.backend == "linear");
  REQUIRE(result.accepted);
  REQUIRE(result.decision == InferenceDecision::Accept);
  REQUIRE(result.score == Catch::Approx(reference.score(features)));
}
