#include "asterion/core/hmac_sha256.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace asterion {

namespace {

constexpr std::array<std::uint32_t, 64> kRoundConstants = {
    0x428a2f98U, 0x71374491U, 0xb5c0fbcfU, 0xe9b5dba5U, 0x3956c25bU, 0x59f111f1U, 0x923f82a4U,
    0xab1c5ed5U, 0xd807aa98U, 0x12835b01U, 0x243185beU, 0x550c7dc3U, 0x72be5d74U, 0x80deb1feU,
    0x9bdc06a7U, 0xc19bf174U, 0xe49b69c1U, 0xefbe4786U, 0x0fc19dc6U, 0x240ca1ccU, 0x2de92c6fU,
    0x4a7484aaU, 0x5cb0a9dcU, 0x76f988daU, 0x983e5152U, 0xa831c66dU, 0xb00327c8U, 0xbf597fc7U,
    0xc6e00bf3U, 0xd5a79147U, 0x06ca6351U, 0x14292967U, 0x27b70a85U, 0x2e1b2138U, 0x4d2c6dfcU,
    0x53380d13U, 0x650a7354U, 0x766a0abbU, 0x81c2c92eU, 0x92722c85U, 0xa2bfe8a1U, 0xa81a664bU,
    0xc24b8b70U, 0xc76c51a3U, 0xd192e819U, 0xd6990624U, 0xf40e3585U, 0x106aa070U, 0x19a4c116U,
    0x1e376c08U, 0x2748774cU, 0x34b0bcb5U, 0x391c0cb3U, 0x4ed8aa4aU, 0x5b9cca4fU, 0x682e6ff3U,
    0x748f82eeU, 0x78a5636fU, 0x84c87814U, 0x8cc70208U, 0x90befffaU, 0xa4506cebU, 0xbef9a3f7U,
    0xc67178f2U};

[[nodiscard]] std::uint32_t rotr(std::uint32_t value, std::uint32_t bits) noexcept {
  return (value >> bits) | (value << (32U - bits));
}

class Sha256Context {
public:
  void update(const std::uint8_t* data, std::size_t length) noexcept {
    total_length_ += length;
    while (length > 0) {
      const std::size_t take = std::min<std::size_t>(64U - buffer_length_, length);
      std::memcpy(buffer_.data() + buffer_length_, data, take);
      buffer_length_ += take;
      data += take;
      length -= take;
      if (buffer_length_ == 64U) {
        process_block(buffer_.data());
        buffer_length_ = 0;
      }
    }
  }

  [[nodiscard]] Sha256Digest finish() noexcept {
    const std::uint64_t bit_length = total_length_ * 8U;
    const std::uint8_t one_bit = 0x80U;
    update(&one_bit, 1U);
    const std::uint8_t zero = 0U;
    while (buffer_length_ != 56U) {
      update(&zero, 1U);
    }
    std::array<std::uint8_t, 8> length_bytes{};
    for (std::size_t i = 0; i < 8U; ++i) {
      length_bytes[i] = static_cast<std::uint8_t>((bit_length >> (56U - 8U * i)) & 0xffU);
    }
    update(length_bytes.data(), length_bytes.size());

    Sha256Digest digest{};
    for (std::size_t i = 0; i < 8U; ++i) {
      digest[i * 4U + 0U] = static_cast<std::uint8_t>((state_[i] >> 24U) & 0xffU);
      digest[i * 4U + 1U] = static_cast<std::uint8_t>((state_[i] >> 16U) & 0xffU);
      digest[i * 4U + 2U] = static_cast<std::uint8_t>((state_[i] >> 8U) & 0xffU);
      digest[i * 4U + 3U] = static_cast<std::uint8_t>(state_[i] & 0xffU);
    }
    return digest;
  }

private:
  void process_block(const std::uint8_t* block) noexcept {
    std::array<std::uint32_t, 64> words{};
    for (std::size_t i = 0; i < 16U; ++i) {
      words[i] = (static_cast<std::uint32_t>(block[i * 4U + 0U]) << 24U) |
                 (static_cast<std::uint32_t>(block[i * 4U + 1U]) << 16U) |
                 (static_cast<std::uint32_t>(block[i * 4U + 2U]) << 8U) |
                 static_cast<std::uint32_t>(block[i * 4U + 3U]);
    }
    for (std::size_t i = 16; i < 64U; ++i) {
      const std::uint32_t s0 =
          rotr(words[i - 15U], 7U) ^ rotr(words[i - 15U], 18U) ^ (words[i - 15U] >> 3U);
      const std::uint32_t s1 =
          rotr(words[i - 2U], 17U) ^ rotr(words[i - 2U], 19U) ^ (words[i - 2U] >> 10U);
      words[i] = words[i - 16U] + s0 + words[i - 7U] + s1;
    }

    std::uint32_t a = state_[0];
    std::uint32_t b = state_[1];
    std::uint32_t c = state_[2];
    std::uint32_t d = state_[3];
    std::uint32_t e = state_[4];
    std::uint32_t f = state_[5];
    std::uint32_t g = state_[6];
    std::uint32_t h = state_[7];

    for (std::size_t i = 0; i < 64U; ++i) {
      const std::uint32_t big_s1 = rotr(e, 6U) ^ rotr(e, 11U) ^ rotr(e, 25U);
      const std::uint32_t ch = (e & f) ^ (~e & g);
      const std::uint32_t temp1 = h + big_s1 + ch + kRoundConstants[i] + words[i];
      const std::uint32_t big_s0 = rotr(a, 2U) ^ rotr(a, 13U) ^ rotr(a, 22U);
      const std::uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
      const std::uint32_t temp2 = big_s0 + maj;
      h = g;
      g = f;
      f = e;
      e = d + temp1;
      d = c;
      c = b;
      b = a;
      a = temp1 + temp2;
    }

    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
  }

  std::array<std::uint32_t, 8> state_{0x6a09e667U, 0xbb67ae85U, 0x3c6ef372U, 0xa54ff53aU,
                                      0x510e527fU, 0x9b05688cU, 0x1f83d9abU, 0x5be0cd19U};
  std::array<std::uint8_t, 64> buffer_{};
  std::size_t buffer_length_{0};
  std::uint64_t total_length_{0};
};

} // namespace

Sha256Digest sha256(std::span<const std::uint8_t> data) noexcept {
  Sha256Context context;
  context.update(data.data(), data.size());
  return context.finish();
}

Sha256Digest sha256(std::string_view data) noexcept {
  return sha256(std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()),
                                              data.size()));
}

Sha256Digest hmac_sha256(std::span<const std::uint8_t> key,
                         std::span<const std::uint8_t> message) noexcept {
  std::array<std::uint8_t, 64> block_key{};
  if (key.size() > block_key.size()) {
    const Sha256Digest hashed = sha256(key);
    std::memcpy(block_key.data(), hashed.data(), hashed.size());
  } else if (!key.empty()) {
    std::memcpy(block_key.data(), key.data(), key.size());
  }

  std::array<std::uint8_t, 64> inner_pad{};
  std::array<std::uint8_t, 64> outer_pad{};
  for (std::size_t i = 0; i < block_key.size(); ++i) {
    inner_pad[i] = static_cast<std::uint8_t>(block_key[i] ^ 0x36U);
    outer_pad[i] = static_cast<std::uint8_t>(block_key[i] ^ 0x5cU);
  }

  Sha256Context inner;
  inner.update(inner_pad.data(), inner_pad.size());
  inner.update(message.data(), message.size());
  const Sha256Digest inner_digest = inner.finish();

  Sha256Context outer;
  outer.update(outer_pad.data(), outer_pad.size());
  outer.update(inner_digest.data(), inner_digest.size());
  return outer.finish();
}

Sha256Digest hmac_sha256(std::string_view key, std::string_view message) noexcept {
  return hmac_sha256(
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(key.data()), key.size()),
      std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(message.data()),
                                    message.size()));
}

std::string to_hex(std::span<const std::uint8_t> bytes) {
  static constexpr char kDigits[] = "0123456789abcdef";
  std::string output;
  output.reserve(bytes.size() * 2U);
  for (const std::uint8_t byte : bytes) {
    output.push_back(kDigits[(byte >> 4U) & 0x0fU]);
    output.push_back(kDigits[byte & 0x0fU]);
  }
  return output;
}

std::string hmac_sha256_hex(std::span<const std::uint8_t> key, std::string_view message) {
  const Sha256Digest digest = hmac_sha256(
      key, std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(message.data()),
                                         message.size()));
  return to_hex(digest);
}

} // namespace asterion
