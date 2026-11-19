#pragma once

#include <cstdint>
#include <numeric>
#include <vector>
#include "esphome/core/frame_reader.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace panasonic_aquarea {
namespace Protocol {
enum class ByteIndex : size_t {
  PREAMBLE = 0,
  PAYLOAD_LENGTH = 1,
  DIRECTION = 2,
  CATEGORY = 3,
};

enum class PreambleByte : uint8_t {
  POLLING = 0x71,  // Polling request/response
  COMMAND = 0xF1,  // Command message
};

enum class ThirdByte : uint8_t {
  X01 = 0x01,
};

enum class CategoryByte : uint8_t {
  STANDARD = 0x10,  // Standard data
  EXTRA = 0x21,     // Extra/extended data
};

static constexpr size_t REQUEST_FRAME_SIZE = 111;
static constexpr size_t RESPONSE_FRAME_SIZE = 203;

template<typename T> static uint8_t calculate_checksum(const T &data) {
  return std::accumulate(data.begin(), data.end(), uint8_t{});
}

using RequestFrame = std::array<uint8_t, REQUEST_FRAME_SIZE>;
using ResponseReader = FrameReader<RESPONSE_FRAME_SIZE, static_cast<uint8_t>(PreambleByte::POLLING),
                                   static_cast<uint8_t>(RESPONSE_FRAME_SIZE - 3), static_cast<uint8_t>(ThirdByte::X01)>;

class Serializer {
 public:
  static RequestFrame polling_message();
  static RequestFrame polling_extra_message();
  static RequestFrame command_message(RequestFrame command_data);

 private:
  static RequestFrame serialize_message(PreambleByte preamble, CategoryByte category, RequestFrame frame);
};
}  // namespace Protocol
}  // namespace panasonic_aquarea
}  // namespace esphome
