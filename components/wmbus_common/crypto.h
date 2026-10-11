#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <span>

namespace wmbus {

std::array<uint8_t, 16> aes_ecb_encrypt(std::span<const uint8_t, 16> key, std::span<const uint8_t, 16> in);

// In place; the tail that does not fill a block is left alone.
void aes_cbc_decrypt(std::span<const uint8_t, 16> key, std::array<uint8_t, 16> iv, std::span<uint8_t> buf);

// RFC 4493.
std::array<uint8_t, 16> aes_cmac(std::span<const uint8_t, 16> key, std::span<const uint8_t> input);

// Over the shorter of the two.
inline void xor_into(std::span<uint8_t> dest, std::span<const uint8_t> src) {
  std::ranges::transform(dest, src, dest.begin(), std::bit_xor{});
}

uint16_t crc16_en13757(std::span<const uint8_t> data);

}  // namespace wmbus
