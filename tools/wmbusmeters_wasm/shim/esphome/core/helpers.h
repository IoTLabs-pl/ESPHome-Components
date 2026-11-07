#pragma once

// The ESPHome helpers wmbus_common uses, with ESPHome's semantics, so the library builds without ESPHome.

#include <cstddef>
#include <cstdint>
#include <string>

namespace esphome {

constexpr uint16_t encode_uint16(uint8_t msb, uint8_t lsb) { return (uint16_t(msb) << 8) | uint16_t(lsb); }

constexpr uint32_t encode_uint32(uint8_t byte1, uint8_t byte2, uint8_t byte3, uint8_t byte4) {
  return (uint32_t(byte1) << 24) | (uint32_t(byte2) << 16) | (uint32_t(byte3) << 8) | uint32_t(byte4);
}

inline uint16_t crc16be(const uint8_t *data, uint16_t len, uint16_t crc = 0, uint16_t poly = 0x1021, bool refin = false,
                        bool refout = false) {
  if (refin)
    crc ^= 0xffff;
  while (len--) {
    crc ^= uint16_t(*data++) << 8;
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x8000) ? uint16_t((crc << 1) ^ poly) : uint16_t(crc << 1);
  }
  return refout ? (crc ^ 0xffff) : crc;
}

inline char format_hex_pretty_char(uint8_t v) { return v >= 10 ? 'A' + (v - 10) : '0' + v; }

inline std::string format_hex_pretty(const uint8_t *data, size_t length, char separator = '.',
                                     bool show_length = true) {
  std::string out;
  for (size_t i = 0; i < length; i++) {
    if (i > 0 && separator)
      out += separator;
    out += format_hex_pretty_char(data[i] >> 4);
    out += format_hex_pretty_char(data[i] & 0x0f);
  }
  if (show_length && length > 4)
    out += " (" + std::to_string(length) + ")";
  return out;
}

}  // namespace esphome
