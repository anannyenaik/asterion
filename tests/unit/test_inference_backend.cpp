#include "asterion/book/order_book.hpp"
#include "asterion/inference/backend.hpp"
#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <vector>

using namespace asterion;

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
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.requested == InferenceBackend::Onnx);
  REQUIRE_FALSE(selection.detail.empty());

  if (kOnnxRuntimeAvailable) {
    // Behaviour with a real ONNX Runtime build depends on the (test-absent) model
    // file, so only assert that a usable model and an honest detail exist.
    SUCCEED("built with ONNX Runtime");
  } else {
    REQUIRE(selection.active == InferenceBackend::Linear);
    REQUIRE(selection.fell_back);
    REQUIRE(selection.model->backend_name() == "linear");
  }
}

TEST_CASE("Selected backend integrates with feature extraction and latency accounting",
          "[inference][backend]") {
  OrderBook book(7);
  REQUIRE(book.add_order(Order{1, 11, 7, Side::Buy, 999, 300, 1, 1}));
  REQUIRE(book.add_order(Order{2, 12, 7, Side::Sell, 1001, 100, 2, 2}));

  FeatureExtractor extractor;
  const std::vector<double> features = extractor.extract_from_book(book);

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
