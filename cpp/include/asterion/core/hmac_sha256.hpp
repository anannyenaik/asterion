#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace asterion {

// Minimal, dependency-free SHA-256 and HMAC-SHA256. This exists so the optional
// audit-manifest signing path does not pull in OpenSSL or any other dependency;
// default CI stays dependency-free. It is a standard implementation of FIPS 180-4
// (SHA-256) and RFC 2104 (HMAC); it makes no cryptographic-compliance claim beyond
// "this is HMAC-SHA256 over the given bytes".

inline constexpr std::size_t kSha256DigestSize = 32;

using Sha256Digest = std::array<std::uint8_t, kSha256DigestSize>;

[[nodiscard]] Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept;
[[nodiscard]] Sha256Digest sha256(std::string_view data) noexcept;

[[nodiscard]] Sha256Digest hmac_sha256(std::span<const std::uint8_t> key,
                                       std::span<const std::uint8_t> message) noexcept;
[[nodiscard]] Sha256Digest hmac_sha256(std::string_view key, std::string_view message) noexcept;

// Lowercase hex encoding of a digest or arbitrary byte span.
[[nodiscard]] std::string to_hex(std::span<const std::uint8_t> bytes);
[[nodiscard]] std::string hmac_sha256_hex(std::span<const std::uint8_t> key,
                                          std::string_view message);

} // namespace asterion
