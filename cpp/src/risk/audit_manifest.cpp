#include "asterion/risk/audit_manifest.hpp"

#include "asterion/core/checksum.hpp"
#include "asterion/core/hmac_sha256.hpp"

#include <charconv>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace asterion {

namespace {

void write_json_string(std::ostream& output, std::string_view value) {
  output << '"';
  for (const char ch : value) {
    switch (ch) {
    case '\\':
      output << "\\\\";
      break;
    case '"':
      output << "\\\"";
      break;
    case '\n':
      output << "\\n";
      break;
    case '\r':
      output << "\\r";
      break;
    case '\t':
      output << "\\t";
      break;
    default:
      output << ch;
      break;
    }
  }
  output << '"';
}

[[nodiscard]] std::optional<std::string_view> json_raw_value(std::string_view line,
                                                             std::string_view key) {
  const std::string needle = "\"" + std::string(key) + "\":";
  const std::size_t key_pos = line.find(needle);
  if (key_pos == std::string_view::npos) {
    return std::nullopt;
  }
  std::size_t begin = key_pos + needle.size();
  if (begin >= line.size()) {
    return std::nullopt;
  }
  if (line[begin] == '"') {
    ++begin;
    std::size_t end = begin;
    bool escaped = false;
    while (end < line.size()) {
      const char ch = line[end];
      if (!escaped && ch == '"') {
        return line.substr(begin, end - begin);
      }
      escaped = (!escaped && ch == '\\');
      ++end;
    }
    return std::nullopt;
  }
  std::size_t end = begin;
  while (end < line.size() && line[end] != ',' && line[end] != '}') {
    ++end;
  }
  return line.substr(begin, end - begin);
}

[[nodiscard]] std::string json_unescape(std::string_view value) {
  std::string output;
  output.reserve(value.size());
  for (std::size_t i = 0; i < value.size(); ++i) {
    if (value[i] == '\\' && i + 1 < value.size()) {
      const char next = value[i + 1];
      switch (next) {
      case 'n':
        output.push_back('\n');
        break;
      case 'r':
        output.push_back('\r');
        break;
      case 't':
        output.push_back('\t');
        break;
      default:
        output.push_back(next);
        break;
      }
      ++i;
    } else {
      output.push_back(value[i]);
    }
  }
  return output;
}

template <typename T> [[nodiscard]] std::optional<T> parse_integral(std::string_view value) {
  T output{};
  const char* first = value.data();
  const char* last = value.data() + value.size();
  const auto [ptr, ec] = std::from_chars(first, last, output);
  if (ec != std::errc() || ptr != last) {
    return std::nullopt;
  }
  return output;
}

template <typename T>
[[nodiscard]] bool assign_integral(std::optional<std::string_view> token, T& output) {
  if (!token) {
    return false;
  }
  const auto parsed = parse_integral<T>(*token);
  if (!parsed) {
    return false;
  }
  output = *parsed;
  return true;
}

[[nodiscard]] std::optional<RiskAuditLogFormat> parse_format_token(std::string_view value) {
  if (value == "jsonl") {
    return RiskAuditLogFormat::Jsonl;
  }
  if (value == "text") {
    return RiskAuditLogFormat::Text;
  }
  return std::nullopt;
}

struct FileScan {
  bool ok{false};
  std::size_t record_count{0};
  std::uintmax_t byte_size{0};
  std::uint64_t content_checksum{0};
  std::string error;
};

[[nodiscard]] FileScan scan_file(const std::filesystem::path& path) {
  FileScan scan;
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    scan.error = "unable to open " + path.string();
    return scan;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::string contents = buffer.str();
  scan.byte_size = static_cast<std::uintmax_t>(contents.size());
  scan.content_checksum = fnv1a_append(kFnvOffsetBasis, contents);

  std::size_t line_start = 0;
  while (line_start <= contents.size()) {
    const std::size_t newline = contents.find('\n', line_start);
    const std::size_t end = newline == std::string::npos ? contents.size() : newline;
    std::string_view line(contents.data() + line_start, end - line_start);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    bool blank = true;
    for (const char ch : line) {
      if (ch != ' ' && ch != '\t') {
        blank = false;
        break;
      }
    }
    if (!blank) {
      ++scan.record_count;
    }
    if (newline == std::string::npos) {
      break;
    }
    line_start = newline + 1;
  }
  scan.ok = true;
  return scan;
}

// Cumulative audit-entry checksum through the first `count` files, best-effort.
// Returns the verifier result so callers can also surface entry counts/validity.
[[nodiscard]] RiskAuditVerificationResult audit_chain_through(
    std::span<const std::filesystem::path> paths, std::size_t count, RiskAuditLogFormat format) {
  return verify_risk_audit_logs(paths.subspan(0, count), format);
}

} // namespace

std::string_view to_string(AuditManifestIssueType type) noexcept {
  switch (type) {
  case AuditManifestIssueType::None:
    return "none";
  case AuditManifestIssueType::MissingFile:
    return "missing_file";
  case AuditManifestIssueType::SizeMismatch:
    return "size_mismatch";
  case AuditManifestIssueType::RecordCountMismatch:
    return "record_count_mismatch";
  case AuditManifestIssueType::ContentChecksumMismatch:
    return "content_checksum_mismatch";
  case AuditManifestIssueType::AuditChainMismatch:
    return "audit_chain_mismatch";
  case AuditManifestIssueType::ManifestChainMismatch:
    return "manifest_chain_mismatch";
  case AuditManifestIssueType::SignatureMismatch:
    return "signature_mismatch";
  case AuditManifestIssueType::SignatureMissing:
    return "signature_missing";
  case AuditManifestIssueType::KeyMissing:
    return "key_missing";
  }
  return "unknown";
}

std::optional<std::vector<std::uint8_t>> read_signing_key_file(
    const std::filesystem::path& path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  const std::string contents = buffer.str();
  return std::vector<std::uint8_t>(contents.begin(), contents.end());
}

std::optional<std::vector<std::uint8_t>> signing_key_from_env(const char* env_var) {
  if (env_var == nullptr) {
    return std::nullopt;
  }
  const char* value = std::getenv(env_var);
  if (value == nullptr) {
    return std::nullopt;
  }
  const std::string_view view(value);
  return std::vector<std::uint8_t>(view.begin(), view.end());
}

std::uint64_t compute_audit_manifest_chain_checksum(
    std::uint32_t schema_version, RiskAuditLogFormat format,
    std::span<const AuditManifestFileEntry> files) noexcept {
  std::uint64_t seed = kFnvOffsetBasis;
  seed = checksum_append(seed, schema_version);
  seed = checksum_append(seed, static_cast<std::uint8_t>(format));
  seed = checksum_append(seed, static_cast<std::uint64_t>(files.size()));
  for (const AuditManifestFileEntry& file : files) {
    seed = checksum_append_string(seed, file.file_name);
    seed = checksum_append(seed, static_cast<std::uint64_t>(file.record_count));
    seed = checksum_append(seed, static_cast<std::uint64_t>(file.byte_size));
    seed = checksum_append(seed, file.content_checksum);
    seed = checksum_append(seed, file.audit_chain_checksum);
  }
  return seed;
}

std::string canonical_audit_manifest_payload(const AuditManifest& manifest) {
  std::ostringstream output;
  output << "asterion-audit-manifest\n";
  output << "schema_version=" << manifest.schema_version << '\n';
  output << "format=" << to_string(manifest.format) << '\n';
  output << "creator=" << manifest.creator << '\n';
  output << "chain_checksum=" << manifest.chain_checksum << '\n';
  for (const AuditManifestFileEntry& file : manifest.files) {
    output << "file=" << file.file_name << ',' << file.record_count << ',' << file.byte_size << ','
           << file.content_checksum << ',' << file.audit_chain_checksum << '\n';
  }
  return output.str();
}

AuditManifestResult generate_audit_manifest(std::span<const std::filesystem::path> paths,
                                            AuditManifestGenerateOptions options) {
  AuditManifestResult result;
  AuditManifest& manifest = result.manifest;
  manifest.schema_version = kAuditManifestSchemaVersion;
  manifest.creator = options.creator;
  manifest.created_at = options.created_at;
  manifest.format = options.format;

  for (std::size_t index = 0; index < paths.size(); ++index) {
    const std::filesystem::path& path = paths[index];
    const FileScan scan = scan_file(path);
    if (!scan.ok) {
      result.ok = false;
      result.error = scan.error;
      return result;
    }
    AuditManifestFileEntry entry;
    entry.file_name = path.filename().string();
    entry.record_count = scan.record_count;
    entry.byte_size = scan.byte_size;
    entry.content_checksum = scan.content_checksum;
    const RiskAuditVerificationResult chain = audit_chain_through(paths, index + 1, options.format);
    if (!chain.valid) {
      result.ok = false;
      result.error = "unable to verify audit chain through " + path.string() + ": " + chain.error;
      return result;
    }
    entry.audit_chain_checksum = chain.final_checksum;
    manifest.files.push_back(std::move(entry));
  }

  manifest.chain_checksum =
      compute_audit_manifest_chain_checksum(manifest.schema_version, manifest.format,
                                            manifest.files);

  if (!options.signing_key.empty()) {
    const std::string payload = canonical_audit_manifest_payload(manifest);
    AuditManifestSignature signature;
    signature.algorithm = std::string(kAuditManifestSignatureAlgorithm);
    signature.key_id = options.signing_key_id;
    signature.value_hex = hmac_sha256_hex(options.signing_key, payload);
    manifest.signature = std::move(signature);
  }

  return result;
}

std::string serialize_audit_manifest(const AuditManifest& manifest) {
  std::ostringstream output;
  output << "{\"record\":\"manifest\",\"schema_version\":" << manifest.schema_version
         << ",\"format\":";
  write_json_string(output, to_string(manifest.format));
  output << ",\"creator\":";
  write_json_string(output, manifest.creator);
  output << ",\"created_at\":";
  write_json_string(output, manifest.created_at);
  output << ",\"chain_checksum\":" << manifest.chain_checksum;
  if (manifest.signature.has_value()) {
    output << ",\"signature_algorithm\":";
    write_json_string(output, manifest.signature->algorithm);
    output << ",\"signature_key_id\":";
    write_json_string(output, manifest.signature->key_id);
    output << ",\"signature\":";
    write_json_string(output, manifest.signature->value_hex);
  }
  output << "}\n";

  for (const AuditManifestFileEntry& file : manifest.files) {
    output << "{\"record\":\"file\",\"file_name\":";
    write_json_string(output, file.file_name);
    output << ",\"record_count\":" << file.record_count << ",\"byte_size\":" << file.byte_size
           << ",\"content_checksum\":" << file.content_checksum
           << ",\"audit_chain_checksum\":" << file.audit_chain_checksum << "}\n";
  }
  return output.str();
}

bool write_audit_manifest(const std::filesystem::path& path, const AuditManifest& manifest) {
  std::ofstream output(path, std::ios::out | std::ios::trunc);
  if (!output) {
    return false;
  }
  output << serialize_audit_manifest(manifest);
  return static_cast<bool>(output);
}

std::optional<AuditManifest> parse_audit_manifest(std::string_view text, std::string* error) {
  const auto fail = [error](std::string message) -> std::optional<AuditManifest> {
    if (error != nullptr) {
      *error = std::move(message);
    }
    return std::nullopt;
  };

  AuditManifest manifest;
  bool header_seen = false;
  std::size_t line_number = 0;
  std::size_t pos = 0;
  while (pos <= text.size()) {
    const std::size_t newline = text.find('\n', pos);
    const std::size_t end = newline == std::string_view::npos ? text.size() : newline;
    std::string_view line = text.substr(pos, end - pos);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    pos = (newline == std::string_view::npos) ? text.size() + 1 : newline + 1;
    if (line.empty()) {
      if (newline == std::string_view::npos) {
        break;
      }
      continue;
    }
    ++line_number;

    const auto record = json_raw_value(line, "record");
    if (!record) {
      return fail("line " + std::to_string(line_number) + ": missing record field");
    }
    if (*record == "manifest") {
      if (header_seen) {
        return fail("line " + std::to_string(line_number) + ": duplicate manifest header");
      }
      header_seen = true;
      if (!assign_integral(json_raw_value(line, "schema_version"), manifest.schema_version) ||
          !assign_integral(json_raw_value(line, "chain_checksum"), manifest.chain_checksum)) {
        return fail("line " + std::to_string(line_number) + ": malformed manifest header");
      }
      const auto format_token = json_raw_value(line, "format");
      const auto format = format_token ? parse_format_token(*format_token)
                                       : std::optional<RiskAuditLogFormat>{};
      if (!format) {
        return fail("line " + std::to_string(line_number) + ": invalid format");
      }
      manifest.format = *format;
      if (const auto creator = json_raw_value(line, "creator")) {
        manifest.creator = json_unescape(*creator);
      }
      if (const auto created_at = json_raw_value(line, "created_at")) {
        manifest.created_at = json_unescape(*created_at);
      }
      const auto signature_value = json_raw_value(line, "signature");
      if (signature_value) {
        AuditManifestSignature signature;
        signature.value_hex = json_unescape(*signature_value);
        if (const auto algorithm = json_raw_value(line, "signature_algorithm")) {
          signature.algorithm = json_unescape(*algorithm);
        }
        if (const auto key_id = json_raw_value(line, "signature_key_id")) {
          signature.key_id = json_unescape(*key_id);
        }
        manifest.signature = std::move(signature);
      }
    } else if (*record == "file") {
      if (!header_seen) {
        return fail("line " + std::to_string(line_number) + ": file entry before manifest header");
      }
      AuditManifestFileEntry entry;
      const auto file_name = json_raw_value(line, "file_name");
      if (!file_name ||
          !assign_integral(json_raw_value(line, "record_count"), entry.record_count) ||
          !assign_integral(json_raw_value(line, "byte_size"), entry.byte_size) ||
          !assign_integral(json_raw_value(line, "content_checksum"), entry.content_checksum) ||
          !assign_integral(json_raw_value(line, "audit_chain_checksum"),
                           entry.audit_chain_checksum)) {
        return fail("line " + std::to_string(line_number) + ": malformed file entry");
      }
      entry.file_name = json_unescape(*file_name);
      manifest.files.push_back(std::move(entry));
    } else {
      return fail("line " + std::to_string(line_number) + ": unknown record type");
    }

    if (newline == std::string_view::npos) {
      break;
    }
  }

  if (!header_seen) {
    return fail("missing manifest header");
  }
  return manifest;
}

std::optional<AuditManifest> read_audit_manifest(const std::filesystem::path& path,
                                                 std::string* error) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    if (error != nullptr) {
      *error = "unable to open manifest: " + path.string();
    }
    return std::nullopt;
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parse_audit_manifest(buffer.str(), error);
}

AuditManifestVerification verify_audit_manifest(const AuditManifest& manifest,
                                                AuditManifestVerifyOptions options) {
  AuditManifestVerification verification;

  const std::uint64_t recomputed_chain = compute_audit_manifest_chain_checksum(
      manifest.schema_version, manifest.format, manifest.files);
  verification.computed_chain_checksum = recomputed_chain;
  if (recomputed_chain != manifest.chain_checksum) {
    verification.valid = false;
    verification.issues.push_back(
        AuditManifestIssue{AuditManifestIssueType::ManifestChainMismatch, "",
                           "manifest chain checksum mismatch: expected " +
                               std::to_string(recomputed_chain) + ", recorded " +
                               std::to_string(manifest.chain_checksum)});
  }

  const std::filesystem::path base =
      options.base_dir.empty() ? std::filesystem::path() : options.base_dir;

  std::vector<std::filesystem::path> present_paths;
  bool all_present = true;
  for (const AuditManifestFileEntry& entry : manifest.files) {
    const std::filesystem::path path = base.empty() ? std::filesystem::path(entry.file_name)
                                                    : base / entry.file_name;
    ++verification.files_checked;
    if (!std::filesystem::exists(path)) {
      verification.valid = false;
      all_present = false;
      verification.issues.push_back(AuditManifestIssue{AuditManifestIssueType::MissingFile,
                                                       entry.file_name, "file not found"});
      continue;
    }
    const FileScan scan = scan_file(path);
    if (!scan.ok) {
      verification.valid = false;
      all_present = false;
      verification.issues.push_back(
          AuditManifestIssue{AuditManifestIssueType::MissingFile, entry.file_name, scan.error});
      continue;
    }
    bool file_ok = true;
    if (scan.byte_size != entry.byte_size) {
      verification.valid = false;
      file_ok = false;
      verification.issues.push_back(AuditManifestIssue{
          AuditManifestIssueType::SizeMismatch, entry.file_name,
          "byte size mismatch: expected " + std::to_string(entry.byte_size) + ", found " +
              std::to_string(scan.byte_size)});
    }
    if (scan.content_checksum != entry.content_checksum) {
      verification.valid = false;
      file_ok = false;
      verification.issues.push_back(
          AuditManifestIssue{AuditManifestIssueType::ContentChecksumMismatch, entry.file_name,
                             "content checksum mismatch"});
    }
    if (scan.record_count != entry.record_count) {
      verification.valid = false;
      file_ok = false;
      verification.issues.push_back(AuditManifestIssue{
          AuditManifestIssueType::RecordCountMismatch, entry.file_name,
          "record count mismatch: expected " + std::to_string(entry.record_count) + ", found " +
              std::to_string(scan.record_count)});
    }
    if (!file_ok) {
      all_present = false;
    }
    present_paths.push_back(path);
  }

  // Semantic audit-chain recompute, only when every file is present and intact.
  if (all_present && !manifest.files.empty()) {
    const RiskAuditVerificationResult chain =
        verify_risk_audit_logs(present_paths, manifest.format);
    verification.entries_checked = chain.entries_checked;
    const std::uint64_t expected = manifest.files.back().audit_chain_checksum;
    if (chain.final_checksum != expected) {
      verification.valid = false;
      verification.issues.push_back(AuditManifestIssue{
          AuditManifestIssueType::AuditChainMismatch, "",
          "audit chain checksum mismatch: expected " + std::to_string(expected) + ", recomputed " +
              std::to_string(chain.final_checksum)});
    }
  }

  if (manifest.signature.has_value()) {
    verification.signature_present = true;
    if (manifest.signature->algorithm != kAuditManifestSignatureAlgorithm) {
      verification.valid = false;
      verification.issues.push_back(AuditManifestIssue{
          AuditManifestIssueType::SignatureMismatch, "",
          "unsupported signature algorithm: " + manifest.signature->algorithm});
    } else if (options.signing_key.empty()) {
      verification.valid = false;
      verification.issues.push_back(AuditManifestIssue{AuditManifestIssueType::KeyMissing, "",
                                                       "signed manifest but no key provided"});
    } else {
      const std::string payload = canonical_audit_manifest_payload(manifest);
      const std::string expected = hmac_sha256_hex(options.signing_key, payload);
      if (expected == manifest.signature->value_hex) {
        verification.signature_valid = true;
      } else {
        verification.valid = false;
        verification.issues.push_back(AuditManifestIssue{AuditManifestIssueType::SignatureMismatch,
                                                         "", "HMAC-SHA256 signature mismatch"});
      }
    }
  } else if (!options.signing_key.empty()) {
    verification.valid = false;
    verification.issues.push_back(AuditManifestIssue{
        AuditManifestIssueType::SignatureMissing, "",
        "key provided but manifest is unsigned (possible signature stripping)"});
  }

  return verification;
}

} // namespace asterion
