#pragma once

#include "asterion/risk/risk_audit.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace asterion {

// Tamper-evident manifest over a set of (possibly rotated) risk audit log files.
//
// The manifest records, for each file: its name, record count, byte size, a
// content checksum over the raw bytes, and the cumulative audit-entry chain
// checksum through the end of that file. A single chain checksum over all file
// entries lets verification detect missing, reordered, edited or truncated files.
//
// Determinism: the chain checksum is computed only from schema version, log format
// and the per-file fields. It never depends on wall-clock time, so identical audit
// streams produce identical manifests. `created_at` is free-form provenance and is
// deliberately excluded from the chain checksum and signature payload; `creator` is
// included in the signature payload when signing is enabled.
//
// Scope: without signing this provides checksum-based integrity evidence, not a
// cryptographic compliance guarantee. Optional HMAC-SHA256 signing (see
// AuditManifestGenerateOptions::signing_key) adds keyed authentication, but only
// the holder of the key can verify it; it is disabled by default.

inline constexpr std::uint32_t kAuditManifestSchemaVersion = 1;
inline constexpr std::string_view kAuditManifestSignatureAlgorithm = "HMAC-SHA256";

struct AuditManifestFileEntry {
  std::string file_name;                  // file name only (no directory component)
  std::size_t record_count{0};            // non-empty lines
  std::uintmax_t byte_size{0};
  std::uint64_t content_checksum{0};      // FNV-1a over the raw file bytes
  std::uint64_t audit_chain_checksum{0};  // cumulative audit-entry checksum through this file
};

struct AuditManifestSignature {
  std::string algorithm;  // always kAuditManifestSignatureAlgorithm when present
  std::string key_id;     // optional caller label; never the key material
  std::string value_hex;  // lowercase hex HMAC-SHA256 over the canonical payload
};

struct AuditManifest {
  std::uint32_t schema_version{kAuditManifestSchemaVersion};
  std::string creator{"asterion"};
  std::string created_at;  // optional provenance; excluded from chain checksum/signature
  RiskAuditLogFormat format{RiskAuditLogFormat::Jsonl};
  std::vector<AuditManifestFileEntry> files;
  std::uint64_t chain_checksum{0};
  std::optional<AuditManifestSignature> signature;
};

struct AuditManifestGenerateOptions {
  RiskAuditLogFormat format{RiskAuditLogFormat::Jsonl};
  std::string creator{"asterion"};
  std::string created_at;  // left empty keeps the manifest fully deterministic
  // Optional HMAC-SHA256 signing key. Empty (default) means no signing. The key is
  // never stored in the manifest; never commit real key material.
  std::vector<std::uint8_t> signing_key;
  std::string signing_key_id;
};

struct AuditManifestResult {
  bool ok{true};
  AuditManifest manifest;
  std::string error;
};

enum class AuditManifestIssueType : std::uint8_t {
  None = 0,
  MissingFile = 1,
  SizeMismatch = 2,
  RecordCountMismatch = 3,
  ContentChecksumMismatch = 4,
  AuditChainMismatch = 5,
  ManifestChainMismatch = 6,
  SignatureMismatch = 7,
  SignatureMissing = 8,
  KeyMissing = 9,
};

[[nodiscard]] std::string_view to_string(AuditManifestIssueType type) noexcept;

struct AuditManifestIssue {
  AuditManifestIssueType type{AuditManifestIssueType::None};
  std::string file_name;  // empty for manifest-level issues
  std::string detail;
};

struct AuditManifestVerification {
  bool valid{true};
  std::size_t files_checked{0};
  std::size_t entries_checked{0};
  std::uint64_t computed_chain_checksum{0};
  bool signature_present{false};
  bool signature_valid{false};
  std::vector<AuditManifestIssue> issues;
};

struct AuditManifestVerifyOptions {
  // Directory the file_name entries are resolved against. Empty means the current
  // directory. Callers that read a manifest file should usually set this to the
  // manifest file's parent directory.
  std::filesystem::path base_dir;
  // Required to verify a signed manifest; ignored when the manifest is unsigned.
  std::vector<std::uint8_t> signing_key;
};

// Load HMAC key material. The key is the exact bytes of the file or environment
// value; nothing is trimmed, so sign and verify must use the same source. Returns
// nullopt when the file is unreadable or the environment variable is unset.
[[nodiscard]] std::optional<std::vector<std::uint8_t>> read_signing_key_file(
    const std::filesystem::path& path);
[[nodiscard]] std::optional<std::vector<std::uint8_t>> signing_key_from_env(
    const char* env_var);

// Deterministic chain checksum over schema version, format and the ordered file
// entries. Excludes created_at and signature.
[[nodiscard]] std::uint64_t compute_audit_manifest_chain_checksum(
    std::uint32_t schema_version, RiskAuditLogFormat format,
    std::span<const AuditManifestFileEntry> files) noexcept;

// Canonical signature-free payload used for HMAC signing and verification. Stable
// across runs and languages so external tooling can reproduce it.
[[nodiscard]] std::string canonical_audit_manifest_payload(const AuditManifest& manifest);

[[nodiscard]] AuditManifestResult generate_audit_manifest(
    std::span<const std::filesystem::path> paths, AuditManifestGenerateOptions options = {});

[[nodiscard]] std::string serialize_audit_manifest(const AuditManifest& manifest);
[[nodiscard]] bool write_audit_manifest(const std::filesystem::path& path,
                                        const AuditManifest& manifest);
[[nodiscard]] std::optional<AuditManifest> parse_audit_manifest(std::string_view text,
                                                                std::string* error);
[[nodiscard]] std::optional<AuditManifest> read_audit_manifest(const std::filesystem::path& path,
                                                               std::string* error);

[[nodiscard]] AuditManifestVerification verify_audit_manifest(
    const AuditManifest& manifest, AuditManifestVerifyOptions options = {});

} // namespace asterion
