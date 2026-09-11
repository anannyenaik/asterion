#include "asterion/core/checksum.hpp"

namespace asterion {

std::uint64_t fnv1a_append_byte(std::uint64_t seed, std::uint8_t byte) noexcept {
  seed ^= static_cast<std::uint64_t>(byte);
  seed *= kFnvPrime;
  return seed;
}

std::uint64_t fnv1a_append(std::uint64_t seed, std::string_view bytes) noexcept {
  for (const unsigned char byte : bytes) {
    seed = fnv1a_append_byte(seed, byte);
  }
  return seed;
}

} // namespace asterion
