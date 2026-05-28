#pragma once

#include <cstdint>
#include <string_view>
#include <type_traits>

namespace asterion {

inline constexpr std::uint64_t kFnvOffsetBasis = 14695981039346656037ull;
inline constexpr std::uint64_t kFnvPrime = 1099511628211ull;

[[nodiscard]] std::uint64_t fnv1a_append_byte(std::uint64_t seed, std::uint8_t byte) noexcept;
[[nodiscard]] std::uint64_t fnv1a_append(std::uint64_t seed, std::string_view bytes) noexcept;

template <typename T>
  requires std::is_integral_v<T>
[[nodiscard]] std::uint64_t checksum_append(std::uint64_t seed, T input) noexcept {
  using Unsigned = std::make_unsigned_t<T>;
  auto value = static_cast<Unsigned>(input);
  for (std::size_t i = 0; i < sizeof(T); ++i) {
    const auto byte = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xffU);
    seed = fnv1a_append_byte(seed, byte);
  }
  return seed;
}

template <typename E>
  requires std::is_enum_v<E>
[[nodiscard]] std::uint64_t checksum_append(std::uint64_t seed, E value) noexcept {
  return checksum_append(seed, static_cast<std::underlying_type_t<E>>(value));
}

[[nodiscard]] inline std::uint64_t checksum_append_string(std::uint64_t seed,
                                                          std::string_view value) noexcept {
  seed = checksum_append(seed, static_cast<std::uint64_t>(value.size()));
  return fnv1a_append(seed, value);
}

} // namespace asterion
