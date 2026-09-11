#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace asterion {

// Minimal SHA-256 and HMAC-SHA256 implementation with no external cryptographic-
// library dependency. It follows FIPS 180-4 and RFC 2104 and carries no compliance
// certification.

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
