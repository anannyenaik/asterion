#include "asterion/inference/model_metadata.hpp"

#include <cctype>
#include <cmath>
#include <fstream>
#include <iterator>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace asterion {
namespace {

[[nodiscard]] std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("unable to read model metadata: " + path.string());
  }
  return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

[[nodiscard]] std::size_t skip_whitespace(std::string_view text, std::size_t position) noexcept {
  while (position < text.size() &&
         std::isspace(static_cast<unsigned char>(text[position])) != 0) {
    ++position;
  }
  return position;
}

[[nodiscard]] std::size_t find_string_end(std::string_view text, std::size_t start) {
  for (std::size_t position = start + 1; position < text.size(); ++position) {
    if (text[position] == '\\') {
      ++position;
      continue;
    }
    if (text[position] == '"') {
      return position;
    }
  }
  throw std::runtime_error("model metadata contains an unterminated string");
}

[[nodiscard]] std::optional<std::size_t> find_value_start(std::string_view text,
                                                          std::string_view key,
                                                          bool required) {
  std::size_t position = 0;
  while ((position = text.find('"', position)) != std::string_view::npos) {
    const std::size_t end = find_string_end(text, position);
    if (text.substr(position + 1, end - position - 1) == key) {
      std::size_t value = skip_whitespace(text, end + 1);
      if (value < text.size() && text[value] == ':') {
        return skip_whitespace(text, value + 1);
      }
    }
    position = end + 1;
  }
  if (required) {
    throw std::runtime_error("model metadata missing field: " + std::string(key));
  }
  return std::nullopt;
}

[[nodiscard]] bool is_value_delimiter(char value) noexcept {
  return std::isspace(static_cast<unsigned char>(value)) != 0 || value == ',' || value == ']' ||
         value == '}';
}

[[nodiscard]] std::string parse_string(std::string_view text, std::size_t position,
                                       std::string_view key) {
  if (position >= text.size() || text[position] != '"') {
    throw std::runtime_error("model metadata field must be a string: " + std::string(key));
  }

  std::string value;
  for (++position; position < text.size(); ++position) {
    const char current = text[position];
    if (current == '"') {
      return value;
    }
    if (current != '\\') {
      value.push_back(current);
      continue;
    }
    if (++position >= text.size()) {
      break;
    }
    const char escaped = text[position];
    switch (escaped) {
    case '"':
    case '\\':
    case '/':
      value.push_back(escaped);
      break;
    case 'b':
      value.push_back('\b');
      break;
    case 'f':
      value.push_back('\f');
      break;
    case 'n':
      value.push_back('\n');
      break;
    case 'r':
      value.push_back('\r');
      break;
    case 't':
      value.push_back('\t');
      break;
    case 'u':
      if (position + 4 >= text.size()) {
        throw std::runtime_error("model metadata field has a truncated unicode escape: " +
                                 std::string(key));
      }
      value.append(text.substr(position - 1, 6));
      position += 4;
      break;
    default:
      throw std::runtime_error("model metadata field has an invalid string escape: " +
                               std::string(key));
    }
  }
  throw std::runtime_error("model metadata field has an unterminated string: " + std::string(key));
}

[[nodiscard]] std::string extract_string(std::string_view text, std::string_view key,
                                         bool required = true) {
  const std::optional<std::size_t> position = find_value_start(text, key, required);
  return position ? parse_string(text, *position, key) : std::string{};
}

[[nodiscard]] bool extract_bool(std::string_view text, std::string_view key, bool required = true) {
  const std::optional<std::size_t> position = find_value_start(text, key, required);
  if (!position) {
    return false;
  }
  if (text.substr(*position, 4) == "true" &&
      (*position + 4 == text.size() || is_value_delimiter(text[*position + 4]))) {
    return true;
  }
  if (text.substr(*position, 5) == "false" &&
      (*position + 5 == text.size() || is_value_delimiter(text[*position + 5]))) {
    return false;
  }
  throw std::runtime_error("model metadata field must be a bool: " + std::string(key));
}

[[nodiscard]] double parse_number(std::string_view text, std::size_t& position,
                                  std::string_view key) {
  const std::size_t start = position;
  if (position < text.size() && text[position] == '-') {
    ++position;
  }
  if (position >= text.size()) {
    throw std::runtime_error("model metadata field has a truncated number: " + std::string(key));
  }

  if (text[position] == '0') {
    ++position;
  } else if (text[position] >= '1' && text[position] <= '9') {
    while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
      ++position;
    }
  } else {
    throw std::runtime_error("model metadata field has an invalid number: " + std::string(key));
  }

  if (position < text.size() && text[position] == '.') {
    ++position;
    const std::size_t fraction_start = position;
    while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
      ++position;
    }
    if (position == fraction_start) {
      throw std::runtime_error("model metadata field has an invalid number: " + std::string(key));
    }
  }

  if (position < text.size() && (text[position] == 'e' || text[position] == 'E')) {
    ++position;
    if (position < text.size() && (text[position] == '+' || text[position] == '-')) {
      ++position;
    }
    const std::size_t exponent_start = position;
    while (position < text.size() && std::isdigit(static_cast<unsigned char>(text[position])) != 0) {
      ++position;
    }
    if (position == exponent_start) {
      throw std::runtime_error("model metadata field has an invalid number: " + std::string(key));
    }
  }

  if (position < text.size() && !is_value_delimiter(text[position])) {
    throw std::runtime_error("model metadata field has an invalid number: " + std::string(key));
  }

  try {
    std::size_t parsed = 0;
    const double value = std::stod(std::string(text.substr(start, position - start)), &parsed);
    if (parsed != position - start || !std::isfinite(value)) {
      throw std::runtime_error("invalid");
    }
    return value;
  } catch (const std::exception&) {
    throw std::runtime_error("model metadata field has an invalid number: " + std::string(key));
  }
}

[[nodiscard]] double extract_double(std::string_view text, std::string_view key,
                                    bool required = true) {
  const std::optional<std::size_t> start = find_value_start(text, key, required);
  if (!start) {
    return 0.0;
  }
  std::size_t position = *start;
  return parse_number(text, position, key);
}

[[nodiscard]] std::uint64_t extract_uint(std::string_view text, std::string_view key,
                                         bool required = true,
                                         std::uint64_t default_value = 0) {
  const std::optional<std::size_t> start = find_value_start(text, key, required);
  if (!start) {
    return default_value;
  }
  std::size_t position = *start;
  const double value = parse_number(text, position, key);
  if (value < 0.0 || std::trunc(value) != value ||
      value >= static_cast<double>(std::numeric_limits<std::uint64_t>::max())) {
    throw std::runtime_error("model metadata field must be a non-negative integer: " +
                             std::string(key));
  }
  return static_cast<std::uint64_t>(value);
}

[[nodiscard]] std::vector<double> extract_double_array(std::string_view text,
                                                       std::string_view key,
                                                       bool required = true) {
  const std::optional<std::size_t> start = find_value_start(text, key, required);
  if (!start) {
    return {};
  }
  std::size_t position = *start;
  if (position >= text.size() || text[position] != '[') {
    throw std::runtime_error("model metadata field must be a numeric array: " + std::string(key));
  }
  position = skip_whitespace(text, position + 1);

  std::vector<double> values;
  if (position < text.size() && text[position] == ']') {
    return values;
  }
  while (position < text.size()) {
    values.push_back(parse_number(text, position, key));
    position = skip_whitespace(text, position);
    if (position >= text.size()) {
      break;
    }
    if (text[position] == ']') {
      return values;
    }
    if (text[position] != ',') {
      throw std::runtime_error("model metadata field has a malformed numeric array: " +
                               std::string(key));
    }
    position = skip_whitespace(text, position + 1);
    if (position >= text.size() || text[position] == ']') {
      throw std::runtime_error("model metadata field has a malformed numeric array: " +
                               std::string(key));
    }
  }
  throw std::runtime_error("model metadata field has a truncated numeric array: " +
                           std::string(key));
}

[[nodiscard]] std::vector<std::int64_t> extract_int_array(std::string_view text,
                                                          std::string_view key) {
  std::vector<std::int64_t> values;
  for (const double value : extract_double_array(text, key)) {
    if (std::trunc(value) != value ||
        value < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        value >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
      throw std::runtime_error("model metadata field must contain integers: " + std::string(key));
    }
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
    const std::size_t positive_dim = static_cast<std::size_t>(dim);
    if (count > std::numeric_limits<std::size_t>::max() / positive_dim) {
      return 0;
    }
    count *= positive_dim;
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
  // window_length is optional; legacy single-timestep artefacts omit it.
  metadata.window_length =
      static_cast<std::size_t>(extract_uint(text, "window_length", false, 1));
  metadata.feature_version = static_cast<std::uint32_t>(extract_uint(text, "feature_version"));
  metadata.onnx_sha256 = extract_string(text, "onnx_sha256", false);
  metadata.source_data_sha256 = extract_string(text, "source_data_sha256", false);
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
  if (metadata.window_length == 0) {
    return {false, "model metadata window_length must be positive"};
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
  if (metadata.feature_count > std::numeric_limits<std::size_t>::max() / metadata.window_length) {
    return {false, "model metadata feature_count * window_length overflows"};
  }
  const std::size_t expected_input_values = metadata.feature_count * metadata.window_length;
  if (input_values != expected_input_values) {
    std::ostringstream error;
    error << "unsupported model shape: input_shape " << shape_to_string(metadata.input_shape)
          << " implies " << input_values << " values but feature_count="
          << metadata.feature_count << " * window_length=" << metadata.window_length << " = "
          << expected_input_values;
    return {false, error.str()};
  }
  if (metadata.artefact_type == "trained_recorded_public_l2" &&
      (metadata.input_shape.size() != 3 || metadata.input_shape[0] != 1 ||
       metadata.input_shape[1] != static_cast<std::int64_t>(metadata.window_length) ||
       metadata.input_shape[2] != static_cast<std::int64_t>(metadata.feature_count))) {
    return {false, "unsupported recorded-public-L2 input_shape " +
                       shape_to_string(metadata.input_shape) + " must be 1x" +
                       std::to_string(metadata.window_length) + "x" +
                       std::to_string(metadata.feature_count)};
  }
  const std::size_t output_values = shape_value_count(metadata.output_shape);
  if (output_values == 0) {
    return {false, "unsupported model shape: output_shape " +
                       shape_to_string(metadata.output_shape) +
                       " must have only fixed positive dimensions"};
  }
  if (metadata.expected_test_input.size() != expected_input_values) {
    return {false, "expected_test_input size does not match input_shape value count"};
  }
  if (metadata.expected_test_output.empty()) {
    return {false, "expected_test_output must not be empty"};
  }
  if (metadata.expected_test_output.size() != output_values) {
    return {false, "expected_test_output size does not match output_shape value count"};
  }
  if (metadata.artefact_type == "trained_recorded_public_l2" &&
      (metadata.output_shape.size() != 2 || metadata.output_shape[0] != 1)) {
    return {false, "recorded-public-L2 output_shape must use a fixed batch dimension of 1"};
  }
  if (!metadata.reference_weights.empty() &&
      metadata.reference_weights.size() != metadata.feature_count) {
    return {false, "reference_weights size does not match feature_count"};
  }
  if (!metadata.artefact_type.empty()) {
    const bool known = metadata.artefact_type == "trained_synthetic_smoke" ||
                       metadata.artefact_type == "exported_untrained_architecture" ||
                       metadata.artefact_type == "deterministic_fixture" ||
                       metadata.artefact_type == "trained_recorded_public_l2";
    if (!known) {
      return {false, "unsupported artefact_type: " + metadata.artefact_type};
    }
    if (metadata.trained_model && metadata.artefact_type == "exported_untrained_architecture") {
      return {false, "artefact_type/trained_model mismatch: trained_model=true but artefact_type="
                         "exported_untrained_architecture"};
    }
    const bool trained_type = metadata.artefact_type == "trained_synthetic_smoke" ||
                              metadata.artefact_type == "trained_recorded_public_l2";
    if (!metadata.trained_model && trained_type) {
      return {false, "artefact_type/trained_model mismatch: trained_model=false but artefact_type=" +
                         metadata.artefact_type};
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
