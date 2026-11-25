#include "crypto.h"

#include "esphome/core/helpers.h"

namespace wmbus {

namespace {

// RFC 4493's subkey step: doubling in GF(2^128).
std::array<uint8_t, 16> doubled(const std::array<uint8_t, 16> &l) {
  std::array<uint8_t, 16> out;
  for (size_t i = 0; i < 15; ++i)
    out[i] = (uint8_t) (l[i] << 1 | l[i + 1] >> 7);
  out[15] = (uint8_t) (l[15] << 1 ^ (l[0] & 0x80 ? 0x87 : 0));
  return out;
}

}  // namespace

std::array<uint8_t, 16> aes_cmac(std::span<const uint8_t, 16> key, std::span<const uint8_t> input) {
  const std::array<uint8_t, 16> k1 = doubled(aes_ecb_encrypt(key, std::array<uint8_t, 16>{}));
  // Where the last block starts; an empty input has an empty one.
  const size_t last = input.empty() ? 0 : (input.size() - 1) / 16 * 16;

  std::array<uint8_t, 16> x{};
  for (size_t i = 0; i < last; i += 16) {
    xor_into(x, input.subspan(i, 16));
    x = aes_ecb_encrypt(key, x);
  }

  const std::span<const uint8_t> tail = input.subspan(last);
  xor_into(x, tail);
  if (tail.size() == 16) {
    xor_into(x, k1);
  } else {
    x[tail.size()] ^= 0x80;
    xor_into(x, doubled(k1));
  }
  return aes_ecb_encrypt(key, x);
}

uint16_t crc16_en13757(std::span<const uint8_t> data) {
  return esphome::crc16be(data.data(), data.size(), 0x0000, 0x3D65, false, true);
}

}  // namespace wmbus
