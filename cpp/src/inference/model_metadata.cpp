#include "asterion/inference/model_metadata.hpp"

#include <fstream>
#include <iterator>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace asterion {
namespace {

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("unable to read model metadata: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::string extract_string(const std::string& text, std::string_view key,
                                         bool required = true) {
  const std::regex pattern("\"" + std::string(key) + "\"\\s*:\\s*\"([^\"]*)\"");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) {
    if (required) {
      throw std::runtime_error("model metadata missing string field: " + std::string(key));
    }
    return {};
  }
  return match[1].str();
}

[[nodiscard]] bool extract_bool(const std::string& text, std::string_view key,
                                bool required = true) {
  const std::regex pattern("\"" + std::string(key) + "\"\\s*:\\s*(true|false)");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) {
    if (required) {
      throw std::runtime_error("model metadata missing bool field: " + std::string(key));
    }
    return false;
  }
  return match[1].str() == "true";
}

[[nodiscard]] double extract_double(const std::string& text, std::string_view key,
                                    bool required = true) {
  const std::regex pattern("\"" + std::string(key) +
                           "\"\\s*:\\s*(-?[0-9]+(?:\\.[0-9]+)?(?:[eE][+-]?[0-9]+)?)");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) {
    if (required) {
      throw std::runtime_error("model metadata missing numeric field: " + std::string(key));
    }
    return 0.0;
  }
  return std::stod(match[1].str());
}

[[nodiscard]] std::uint64_t extract_uint(const std::string& text, std::string_view key) {
  const double value = extract_double(text, key);
  if (value < 0.0) {
    throw std::runtime_error("model metadata field must be non-negative: " + std::string(key));
  }
  return static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::string extract_array_body(const std::string& text, std::string_view key,
                                             bool required = true) {
  const std::regex pattern("\"" + std::string(key) + "\"\\s*:\\s*\\[([^\\]]*)\\]");
  std::smatch match;
  if (!std::regex_search(text, match, pattern)) {
    if (required) {
      throw std::runtime_error("model metadata missing array field: " + std::string(key));
    }
    return {};
  }
  return match[1].str();
}

[[nodiscard]] std::vector<double> extract_double_array(const std::string& text,
                                                       std::string_view key,
                                                       bool required = true) {
  std::vector<double> values;
  std::stringstream stream(extract_array_body(text, key, required));
  std::string token;
  while (std::getline(stream, token, ',')) {
    std::stringstream trimmed(token);
    double value = 0.0;
    trimmed >> value;
    if (!trimmed.fail()) {
      values.push_back(value);
    }
  }
  return values;
}

[[nodiscard]] std::vector<std::int64_t> extract_int_array(const std::string& text,
                                                          std::string_view key) {
  std::vector<std::int64_t> values;
  for (const double value : extract_double_array(text, key)) {
    values.push_back(static_cast<std::int64_t>(value));
  }
  return values;
}

} // namespace

std::size_t shape_value_count(std::span<const std::int64_t> shape) noexcept {
  if (shape.empty()) {
    return 0;
  }
  std::size_t count = 1;
  for (const std::int64_t dim : shape) {
    if (dim <= 0) {
      return 0;
    }
    count *= static_cast<std::size_t>(dim);
  }
  return count;
}

std::string shape_to_string(std::span<const std::int64_t> shape) {
  if (shape.empty()) {
    return "n/a";
  }
  std::ostringstream output;
  for (std::size_t i = 0; i < shape.size(); ++i) {
    if (i > 0) {
      output << 'x';
    }
    output << shape[i];
  }
  return output.str();
}

ModelMetadata load_model_metadata(const std::filesystem::path& path) {
  const std::string text = read_text(path);
  ModelMetadata metadata;
  metadata.model_name = extract_string(text, "model_name");
  metadata.model_class = extract_string(text, "model_class", false);
  metadata.artefact_type = extract_string(text, "artefact_type", false);
  metadata.source_repo_path = extract_string(text, "source_repo_path", false);
  metadata.source_commit = extract_string(text, "source_commit", false);
  metadata.source_dirty = extract_bool(text, "source_dirty", false);
  metadata.export_command = extract_string(text, "export_command");
  metadata.input_shape = extract_int_array(text, "input_shape");
  metadata.output_shape = extract_int_array(text, "output_shape");
  metadata.feature_count = static_cast<std::size_t>(extract_uint(text, "feature_count"));
  metadata.feature_version = static_cast<std::uint32_t>(extract_uint(text, "feature_version"));
  metadata.expected_test_input = extract_double_array(text, "expected_test_input");
  metadata.expected_test_output = extract_double_array(text, "expected_test_output");
  metadata.reference_weights = extract_double_array(text, "reference_weights", false);
  metadata.reference_bias = extract_double(text, "reference_bias", false);
  metadata.trained_model = extract_bool(text, "trained_model");
  metadata.deterministic_fixture = extract_bool(text, "deterministic_fixture", false);
  metadata.claim_boundary = extract_string(text, "claim_boundary", false);
  return metadata;
}

ModelMetadataValidation validate_model_metadata(const ModelMetadata& metadata) {
  if (metadata.model_name.empty()) {
    return {false, "model metadata missing model_name"};
  }
  if (metadata.input_shape.empty()) {
    return {false, "model metadata missing input_shape"};
  }
  if (metadata.output_shape.empty()) {
    return {false, "model metadata missing output_shape"};
  }
  if (metadata.feature_count == 0) {
    return {false, "model metadata feature_count must be positive"};
  }
  if (metadata.feature_version == 0) {
    return {false, "model metadata feature_version must be positive"};
  }
  const std::size_t input_values = shape_value_count(metadata.input_shape);
  if (input_values == 0) {
    return {false, "unsupported model shape: input_shape " +
                       shape_to_string(metadata.input_shape) +
                       " must have only fixed positive dimensions"};
  }
  if (input_values != metadata.feature_count) {
    std::ostringstream error;
    error << "unsupported model shape: input_shape " << shape_to_string(metadata.input_shape)
          << " implies " << input_values << " values but feature_count="
          << metadata.feature_count;
    return {false, error.str()};
  }
  if (shape_value_count(metadata.output_shape) == 0) {
    return {false, "unsupported model shape: output_shape " +
                       shape_to_string(metadata.output_shape) +
                       " must have only fixed positive dimensions"};
  }
  if (metadata.expected_test_input.size() != metadata.feature_count) {
    return {false, "expected_test_input size does not match feature_count"};
  }
  if (metadata.expected_test_output.empty()) {
    return {false, "expected_test_output must not be empty"};
  }
  if (!metadata.reference_weights.empty() &&
      metadata.reference_weights.size() != metadata.feature_count) {
    return {false, "reference_weights size does not match feature_count"};
  }
  if (!metadata.artefact_type.empty()) {
    const bool known = metadata.artefact_type == "trained_synthetic_smoke" ||
                       metadata.artefact_type == "exported_untrained_architecture" ||
                       metadata.artefact_type == "deterministic_fixture";
    if (!known) {
      return {false, "unsupported artefact_type: " + metadata.artefact_type};
    }
    if (metadata.trained_model && metadata.artefact_type == "exported_untrained_architecture") {
      return {false, "artefact_type/trained_model mismatch: trained_model=true but artefact_type="
                         "exported_untrained_architecture"};
    }
    if (!metadata.trained_model && metadata.artefact_type == "trained_synthetic_smoke") {
      return {false, "artefact_type/trained_model mismatch: trained_model=false but artefact_type="
                         "trained_synthetic_smoke"};
    }
  }
  return {true, {}};
}

ModelMetadataValidation validate_feature_compatibility(const ModelMetadata& metadata,
                                                       std::size_t feature_count,
                                                       std::uint32_t feature_version) {
  if (metadata.feature_count != feature_count) {
    std::ostringstream error;
    error << "feature count mismatch: Asterion feature_count=" << feature_count
          << " model feature_count=" << metadata.feature_count;
    return {false, error.str()};
  }
  if (metadata.feature_version != feature_version) {
    std::ostringstream error;
    error << "feature version mismatch: Asterion feature_version=" << feature_version
          << " model feature_version=" << metadata.feature_version;
    return {false, error.str()};
  }
  return {true, {}};
}

double score_reference_fixture(const ModelMetadata& metadata, std::span<const double> features) {
  if (features.size() != metadata.reference_weights.size()) {
    throw std::invalid_argument("reference fixture feature count mismatch");
  }
  double score = metadata.reference_bias;
  for (std::size_t i = 0; i < features.size(); ++i) {
    score += metadata.reference_weights[i] * features[i];
  }
  return score;
}

} // namespace asterion
