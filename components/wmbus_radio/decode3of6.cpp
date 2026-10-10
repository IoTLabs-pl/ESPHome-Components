#include "decode3of6.h"

#include <algorithm>
#include <array>

#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

namespace esphome {
namespace wmbus_radio {
static const char *TAG = "3of6";
static constexpr std::array<uint8_t, 16> CODES = {
    0b010110, 0b001101, 0b001110, 0b001011, 0b011100, 0b011001, 0b011010, 0b010011,
    0b101100, 0b100101, 0b100110, 0b100011, 0b110100, 0b110001, 0b110010, 0b101001,
};

std::optional<std::vector<uint8_t>> decode3of6(std::span<const uint8_t> coded_data) {
  // ESP_LOGD(TAG, "Decoding 3of6 data: %s", format_hex(coded_data).c_str());

  std::vector<uint8_t> decodedBytes;
  auto segments = coded_data.size() * 8 / 6;
  auto data = coded_data.data();

  for (size_t i = 0; i < segments; i++) {
    auto bit_idx = i * 6;
    auto byte_idx = bit_idx / 8;
    auto bit_offset = bit_idx % 8;

    uint8_t code = (data[byte_idx] << bit_offset);
    if (bit_offset > 0)
      code |= (data[byte_idx + 1] >> (8 - bit_offset));
    code >>= 2;

    auto it = std::ranges::find(CODES, code);
    if (it == CODES.end()) {
      // ESP_LOGW(TAG, "Invalid code: 0x%02X", code);
      return {};
    }
    uint8_t nibble = it - CODES.begin();

    if (i % 2 == 0)
      decodedBytes.push_back(nibble << 4);
    else
      decodedBytes.back() |= nibble;
  }

  // ESP_LOGV(TAG, "Successfully decoded %zu bytes", decodedBytes.size());
  return decodedBytes;
}

size_t encoded_size(size_t decoded_size) {
  // Every 2 bytes (4 nibbles by 6 bits = 24b) of decoded data is encoded into 3 bytes of coded data
  // +1 for rounding up
  return (3 * decoded_size + 1) / 2;
}
}  // namespace wmbus_radio
}  // namespace esphome