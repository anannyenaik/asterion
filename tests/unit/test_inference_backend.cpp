#include "asterion/book/order_book.hpp"
#include "asterion/core/allocation_tracker.hpp"
#include "asterion/inference/backend.hpp"
#include "asterion/inference/feature_extractor.hpp"
#include "asterion/inference/inference.hpp"
#include "asterion/inference/linear_model.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace asterion;

namespace {

#if defined(ASTERION_HAVE_ONNXRUNTIME)
std::vector<unsigned char> decode_base64_fixture(const std::filesystem::path& path) {
  std::ifstream input(path);
  REQUIRE(input);
  std::string encoded((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
  std::vector<unsigned char> output;
  int value = 0;
  int bits = -8;
  for (const unsigned char ch : encoded) {
    if (std::isspace(ch)) {
      continue;
    }
    if (ch == '=') {
      break;
    }
    int digit = -1;
    if (ch >= 'A' && ch <= 'Z') {
      digit = ch - 'A';
    } else if (ch >= 'a' && ch <= 'z') {
      digit = ch - 'a' + 26;
    } else if (ch >= '0' && ch <= '9') {
      digit = ch - '0' + 52;
    } else if (ch == '+') {
      digit = 62;
    } else if (ch == '/') {
      digit = 63;
    }
    REQUIRE(digit >= 0);
    value = (value << 6) + digit;
    bits += 6;
    if (bits >= 0) {
      output.push_back(static_cast<unsigned char>((value >> bits) & 0xff));
      bits -= 8;
    }
  }
  return output;
}

std::filesystem::path write_temp_onnx_fixture() {
  const auto bytes =
      decode_base64_fixture(std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "fixtures" /
                            "identity_1x4.onnx.b64");
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path path =
      std::filesystem::temp_directory_path() /
      ("asterion_identity_" + std::to_string(stamp) + ".onnx");
  std::ofstream output(path, std::ios::binary);
  REQUIRE(output);
  output.write(reinterpret_cast<const char*>(bytes.data()),
               static_cast<std::streamsize>(bytes.size()));
  return path;
}
#endif

} // namespace

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
  const std::filesystem::path model_path = write_temp_onnx_fixture();

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = model_path;
  config.linear_weights = {99.0, 99.0, 99.0, 99.0};
  config.linear_bias = 99.0;

  const InferenceBackendSelection selection = make_inference_backend(config);
  INFO(selection.detail);
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.requested == InferenceBackend::Onnx);
  REQUIRE(selection.active == InferenceBackend::Onnx);
  REQUIRE_FALSE(selection.fell_back);
  REQUIRE(selection.model->backend_name() == "onnx");

  const std::array<double, 4> features{3.5, 2.0, 1.0, -1.0};
  const double first_score = selection.model->score(features);
  const double second_score = selection.model->score(features);
  // The identity fixture returns its first input element; scoring must be deterministic.
  REQUIRE(first_score == Catch::Approx(3.5));
  REQUIRE(first_score == Catch::Approx(second_score));

  MeasuredInferenceEngine engine(*selection.model, InferencePolicy{1'000'000'000, 0, true, true});
  const InferenceResult result = engine.score(features);
  REQUIRE(result.backend == "onnx");
  REQUIRE(result.accepted);
  REQUIRE(result.decision == InferenceDecision::Accept);
  REQUIRE(result.score == Catch::Approx(3.5));

  std::filesystem::remove(model_path);
}

TEST_CASE("ONNX inference allocations are measured honestly and separated from load",
          "[inference][backend][onnx][alloc]") {
  const std::filesystem::path model_path = write_temp_onnx_fixture();

  InferenceBackendConfig config;
  config.requested = InferenceBackend::Onnx;
  config.model_path = model_path;
  config.linear_weights = {1.0, 0.0, 0.0, 0.0};
  config.linear_bias = 0.0;

  // Model-load allocations (session/graph setup) are expected and are measured
  // separately from steady-state inference; we do not require them to be zero.
  reset_allocation_counters();
  const InferenceBackendSelection selection = make_inference_backend(config);
  const AllocationSnapshot load_snapshot = allocation_snapshot();
  REQUIRE(selection.model != nullptr);
  REQUIRE(selection.active == InferenceBackend::Onnx);

  const std::array<double, 4> features{3.5, 2.0, 1.0, -1.0};

  // Warm up once so any lazy first-call allocations are not counted as steady state.
  const double warm = selection.model->score(features);
  REQUIRE(warm == Catch::Approx(3.5));

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
  REQUIRE(last == Catch::Approx(3.5)); // deterministic scoring
  // The allocation counts above are informational and reported via INFO; we do
  // not assert ONNX inference is allocation-free because that is not proven.
  // Determinism is the real invariant being tested here.
  (void)steady_snapshot;
  (void)load_snapshot;

  std::filesystem::remove(model_path);
}
#endif

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
