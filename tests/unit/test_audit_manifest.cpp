#include "asterion/core/hmac_sha256.hpp"
#include "asterion/risk/audit_manifest.hpp"
#include "asterion/risk/risk_gateway.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace asterion;

namespace {

std::filesystem::path make_temp_dir() {
  const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
  const std::filesystem::path dir =
      std::filesystem::temp_directory_path() / ("asterion_manifest_" + std::to_string(stamp));
  std::filesystem::create_directories(dir);
  return dir;
}

NewOrderRequest limit_buy(ClientOrderId id, TimestampNs now_ns) {
  return NewOrderRequest{id, 1, Side::Buy, OrderType::Limit, 1000, 10, now_ns, 7};
}

std::vector<std::filesystem::path> make_rotating_logs(const std::filesystem::path& base,
                                                      ClientOrderId orders) {
  RiskGateway risk;
  REQUIRE(risk.open_rotating_audit_log(base, RiskAuditLogFormat::Jsonl, 2, 0));
  risk.on_market_data(1, 1000, 0);
  for (ClientOrderId id = 1; id <= orders; ++id) {
    REQUIRE(risk.check_new_order(limit_buy(id, static_cast<TimestampNs>(id)),
                                 static_cast<TimestampNs>(id))
                .accepted);
  }
  risk.close_audit_log();
  return risk.audit_log_paths();
}

bool has_issue(const AuditManifestVerification& verification, AuditManifestIssueType type) {
  for (const AuditManifestIssue& issue : verification.issues) {
    if (issue.type == type) {
      return true;
    }
  }
  return false;
}

void remove_dir(const std::filesystem::path& dir) {
  std::error_code ec;
  std::filesystem::remove_all(dir, ec);
}

} // namespace

TEST_CASE("SHA-256 matches the known test vector", "[crypto][sha256]") {
  REQUIRE(to_hex(sha256("abc")) ==
          "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
  REQUIRE(to_hex(sha256("")) ==
          "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
}

TEST_CASE("HMAC-SHA256 matches the known RFC test vector", "[crypto][hmac]") {
  REQUIRE(hmac_sha256_hex(
              std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>("key"), 3),
              "The quick brown fox jumps over the lazy dog") ==
          "f7bc83f430538424b13298e6aa6fb143ef4d59a14946175997479dbc2d1a3cd8");
}

TEST_CASE("Audit manifest verifies clean rotated logs", "[audit][manifest]") {
  const std::filesystem::path dir = make_temp_dir();
  const auto paths = make_rotating_logs(dir / "audit.jsonl", 5);
  REQUIRE(paths.size() == 3);

  const AuditManifestResult generated = generate_audit_manifest(paths);
  REQUIRE(generated.ok);
  REQUIRE(generated.manifest.files.size() == 3);
  REQUIRE(generated.manifest.chain_checksum != 0);
  REQUIRE_FALSE(generated.manifest.signature.has_value());

  AuditManifestVerifyOptions options;
  options.base_dir = dir;
  const AuditManifestVerification verification =
      verify_audit_manifest(generated.manifest, options);
  REQUIRE(verification.valid);
  REQUIRE(verification.files_checked == 3);
  REQUIRE(verification.entries_checked == 5);
  REQUIRE(verification.issues.empty());

  remove_dir(dir);
}

TEST_CASE("Audit manifest round-trips through serialization", "[audit][manifest]") {
  const std::filesystem::path dir = make_temp_dir();
  const auto paths = make_rotating_logs(dir / "audit.jsonl", 5);

  const AuditManifest manifest = generate_audit_manifest(paths).manifest;
  const std::string text = serialize_audit_manifest(manifest);
  std::string error;
  const auto parsed = parse_audit_manifest(text, &error);
  INFO(error);
  REQUIRE(parsed.has_value());
  REQUIRE(parsed->schema_version == manifest.schema_version);
  REQUIRE(parsed->chain_checksum == manifest.chain_checksum);
  REQUIRE(parsed->files.size() == manifest.files.size());
  REQUIRE(parsed->files.front().file_name == manifest.files.front().file_name);
  REQUIRE(parsed->files.back().content_checksum == manifest.files.back().content_checksum);

  AuditManifestVerifyOptions options;
  options.base_dir = dir;
  REQUIRE(verify_audit_manifest(*parsed, options).valid);
  remove_dir(dir);
}

TEST_CASE("Audit manifest detects an edited file", "[audit][manifest][tamper]") {
  const std::filesystem::path dir = make_temp_dir();
  const auto paths = make_rotating_logs(dir / "audit.jsonl", 5);
  const AuditManifest manifest = generate_audit_manifest(paths).manifest;

  // Flip a character inside the first rotated file without changing its size.
  {
    std::fstream file(paths.front(), std::ios::in | std::ios::out | std::ios::binary);
    REQUIRE(file);
    file.seekp(0, std::ios::beg);
    char first = 0;
    file.get(first);
    file.seekp(0, std::ios::beg);
    file.put(first == 'x' ? 'y' : 'x');
  }

  AuditManifestVerifyOptions options;
  options.base_dir = dir;
  const AuditManifestVerification verification = verify_audit_manifest(manifest, options);
  REQUIRE_FALSE(verification.valid);
  REQUIRE(has_issue(verification, AuditManifestIssueType::ContentChecksumMismatch));
  remove_dir(dir);
}

TEST_CASE("Audit manifest detects a truncated file", "[audit][manifest][tamper]") {
  const std::filesystem::path dir = make_temp_dir();
  const auto paths = make_rotating_logs(dir / "audit.jsonl", 5);
  const AuditManifest manifest = generate_audit_manifest(paths).manifest;

  std::filesystem::resize_file(paths.front(), 10);

  AuditManifestVerifyOptions options;
  options.base_dir = dir;
  const AuditManifestVerification verification = verify_audit_manifest(manifest, options);
  REQUIRE_FALSE(verification.valid);
  REQUIRE(has_issue(verification, AuditManifestIssueType::SizeMismatch));
  remove_dir(dir);
}

TEST_CASE("Audit manifest detects a missing file", "[audit][manifest][tamper]") {
  const std::filesystem::path dir = make_temp_dir();
  const auto paths = make_rotating_logs(dir / "audit.jsonl", 5);
  const AuditManifest manifest = generate_audit_manifest(paths).manifest;

  std::filesystem::remove(paths.back());

  AuditManifestVerifyOptions options;
  options.base_dir = dir;
  const AuditManifestVerification verification = verify_audit_manifest(manifest, options);
  REQUIRE_FALSE(verification.valid);
  REQUIRE(has_issue(verification, AuditManifestIssueType::MissingFile));
  remove_dir(dir);
}

TEST_CASE("Audit manifest detects reordered file entries", "[audit][manifest][tamper]") {
  const std::filesystem::path dir = make_temp_dir();
  const auto paths = make_rotating_logs(dir / "audit.jsonl", 5);
  AuditManifest manifest = generate_audit_manifest(paths).manifest;

  // Reorder the recorded file entries without recomputing the chain checksum.
  std::swap(manifest.files.front(), manifest.files.back());

  AuditManifestVerifyOptions options;
  options.base_dir = dir;
  const AuditManifestVerification verification = verify_audit_manifest(manifest, options);
  REQUIRE_FALSE(verification.valid);
  REQUIRE(has_issue(verification, AuditManifestIssueType::ManifestChainMismatch));
  remove_dir(dir);
}

TEST_CASE("Audit manifest HMAC signing round-trips and rejects tampering",
          "[audit][manifest][signing]") {
  const std::filesystem::path dir = make_temp_dir();
  const auto paths = make_rotating_logs(dir / "audit.jsonl", 5);

  const std::filesystem::path key_path =
      std::filesystem::path(ASTERION_SOURCE_DIR) / "data" / "fixtures" / "audit_signing_test.key";
  const auto key = read_signing_key_file(key_path);
  REQUIRE(key.has_value());
  REQUIRE_FALSE(key->empty());

  AuditManifestGenerateOptions generate_options;
  generate_options.signing_key = *key;
  generate_options.signing_key_id = "test-fixture";
  const AuditManifestResult generated = generate_audit_manifest(paths, generate_options);
  REQUIRE(generated.ok);
  REQUIRE(generated.manifest.signature.has_value());
  REQUIRE(generated.manifest.signature->algorithm == "HMAC-SHA256");

  AuditManifestVerifyOptions verify_options;
  verify_options.base_dir = dir;
  verify_options.signing_key = *key;
  const AuditManifestVerification ok = verify_audit_manifest(generated.manifest, verify_options);
  REQUIRE(ok.valid);
  REQUIRE(ok.signature_present);
  REQUIRE(ok.signature_valid);

  // No key provided => cannot authenticate a signed manifest.
  AuditManifestVerifyOptions no_key;
  no_key.base_dir = dir;
  const AuditManifestVerification missing_key =
      verify_audit_manifest(generated.manifest, no_key);
  REQUIRE_FALSE(missing_key.valid);
  REQUIRE(has_issue(missing_key, AuditManifestIssueType::KeyMissing));

  // Wrong key => signature mismatch.
  AuditManifestVerifyOptions wrong_key;
  wrong_key.base_dir = dir;
  const std::string other = "totally-different-key";
  wrong_key.signing_key.assign(other.begin(), other.end());
  const AuditManifestVerification mismatch =
      verify_audit_manifest(generated.manifest, wrong_key);
  REQUIRE_FALSE(mismatch.valid);
  REQUIRE(has_issue(mismatch, AuditManifestIssueType::SignatureMismatch));

  // Unsigned manifest but a key is supplied => possible signature stripping.
  const AuditManifest unsigned_manifest = generate_audit_manifest(paths).manifest;
  AuditManifestVerifyOptions expect_signed;
  expect_signed.base_dir = dir;
  expect_signed.signing_key = *key;
  const AuditManifestVerification stripped =
      verify_audit_manifest(unsigned_manifest, expect_signed);
  REQUIRE_FALSE(stripped.valid);
  REQUIRE(has_issue(stripped, AuditManifestIssueType::SignatureMissing));

  remove_dir(dir);
}

TEST_CASE("Audit manifest signature covers stable provenance and rejects unsupported algorithm",
          "[audit][manifest][signing]") {
  const std::filesystem::path dir = make_temp_dir();
  const auto paths = make_rotating_logs(dir / "audit.jsonl", 3);

  const std::string key_text = "phase9-signing-fixture";
  AuditManifestGenerateOptions generate_options;
  generate_options.creator = "phase9-test";
  generate_options.created_at = "2026-05-29T00:00:00Z";
  generate_options.signing_key.assign(key_text.begin(), key_text.end());
  generate_options.signing_key_id = "phase9";

  AuditManifest manifest = generate_audit_manifest(paths, generate_options).manifest;
  AuditManifestVerifyOptions verify_options;
  verify_options.base_dir = dir;
  verify_options.signing_key = generate_options.signing_key;
  REQUIRE(verify_audit_manifest(manifest, verify_options).valid);

  manifest.created_at = "changed-clock-provenance";
  REQUIRE(verify_audit_manifest(manifest, verify_options).valid);

  manifest.creator = "tampered-creator";
  const AuditManifestVerification creator_tampered =
      verify_audit_manifest(manifest, verify_options);
  REQUIRE_FALSE(creator_tampered.valid);
  REQUIRE(has_issue(creator_tampered, AuditManifestIssueType::SignatureMismatch));

  manifest = generate_audit_manifest(paths, generate_options).manifest;
  REQUIRE(manifest.signature.has_value());
  manifest.signature->algorithm = "HMAC-SHA1";
  const AuditManifestVerification wrong_algorithm =
      verify_audit_manifest(manifest, verify_options);
  REQUIRE_FALSE(wrong_algorithm.valid);
  REQUIRE(has_issue(wrong_algorithm, AuditManifestIssueType::SignatureMismatch));

  remove_dir(dir);
}
