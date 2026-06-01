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
#include <span>
#include <string>
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

std::filesystem::path chronoslob_real_model_path() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models" /
         "chronoslob_tiny_real.onnx";
}

std::filesystem::path chronoslob_real_metadata_path() {
  return std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "models" /
         "chronoslob_tiny_real.metadata.json";
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

TEST_CASE("ChronosLOB real trained artefact metadata loads and validates",
          "[inference][backend][metadata][real]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_real_metadata_path());
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE(validation.ok);
  REQUIRE(validation.error.empty());
  REQUIRE(metadata.model_name == "chronoslob_tiny_real");
  REQUIRE(metadata.model_class == "DeepLOBModel");
  REQUIRE(metadata.artefact_type == "trained_synthetic_smoke");
  REQUIRE(metadata.trained_model == true);
  REQUIRE(metadata.deterministic_fixture == false);
  REQUIRE(shape_to_string(metadata.input_shape) == "1x1x4");
  REQUIRE(shape_to_string(metadata.output_shape) == "1x3");
  REQUIRE(metadata.feature_count == kL2FeatureCount);
  REQUIRE(metadata.feature_version == kL2FeatureVersion);
  // A real exported model carries no hand-written linear head.
  REQUIRE(metadata.reference_weights.empty());
  // The 4 caller-owned L2 features feed the [1, 1, 4] single-timestep input.
  REQUIRE(shape_value_count(metadata.input_shape) == kL2FeatureCount);
  REQUIRE(metadata.expected_test_input.size() == kL2FeatureCount);
  REQUIRE(metadata.expected_test_output.size() == 3);
  REQUIRE(validate_feature_compatibility(metadata, kL2FeatureCount, kL2FeatureVersion).ok);
}

TEST_CASE("Model metadata rejects an input shape that does not match feature_count",
          "[inference][backend][metadata][real]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_real_metadata_path());
  metadata.input_shape = {1, 1, 8}; // 8 values but feature_count is 4
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("unsupported model shape") != std::string::npos);
}

TEST_CASE("Model metadata rejects a trained_model/artefact_type mismatch",
          "[inference][backend][metadata][real]") {
  ModelMetadata metadata = load_model_metadata(chronoslob_real_metadata_path());
  metadata.artefact_type = "exported_untrained_architecture"; // inconsistent with trained_model
  const ModelMetadataValidation validation = validate_model_metadata(metadata);
  REQUIRE_FALSE(validation.ok);
  REQUIRE(validation.error.find("artefact_type/trained_model mismatch") != std::string::npos);
}

TEST_CASE("ONNX request for the real model with a mismatched feature count falls back clearly",
          "[inference][backend][fallback][real]") {
  // Deterministic regardless of whether ONNX Runtime is compiled in: the feature
  // contract is checked before any model load is attempted.
  const ModelMetadata metadata = load_model_metadata(chronoslob_real_metadata_path());
  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_real_model_path();
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

TEST_CASE("ONNX inference allocations are measured honestly and separated from load",
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
  // zero. ONNX Runtime is free to allocate per-run buffers; honesty over claims.
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
  // Determinism is the real invariant being tested here.
  (void)steady_snapshot;
  (void)load_snapshot;

}

TEST_CASE("ONNX Runtime loads the real ChronosLOB DeepLOB artefact and scores deterministically",
          "[inference][backend][onnx][real]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_real_metadata_path());
  REQUIRE(validate_model_metadata(metadata).ok);
  REQUIRE(metadata.trained_model);

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_real_model_path();
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
  REQUIRE(result.model_name == "chronoslob_tiny_real.onnx");
  REQUIRE(result.input_shape == "1x1x4");
  REQUIRE(result.output_shape == "1x3");
  REQUIRE(result.accepted);
  REQUIRE(result.decision == InferenceDecision::Accept);
  REQUIRE(result.score == Catch::Approx(metadata.expected_test_output.front()).margin(1e-3));
}

TEST_CASE("Real ChronosLOB ONNX load allocations are separated from steady-state inference",
          "[inference][backend][onnx][alloc][real]") {
  const ModelMetadata metadata = load_model_metadata(chronoslob_real_metadata_path());

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = chronoslob_real_model_path();
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
  INFO("real-model onnx load allocations=" << load_snapshot.allocations << " bytes="
       << load_snapshot.bytes_allocated << "; steady-state allocations="
       << steady_snapshot.allocations << " over 16 scores");
  REQUIRE(last == Catch::Approx(metadata.expected_test_output.front()).margin(1e-3));
  (void)steady_snapshot;
  (void)load_snapshot;
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
