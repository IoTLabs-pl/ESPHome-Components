#pragma once

#include <forward_list>
#include <vector>
#include <array>
#include <cstring>
#include <cstdint>
#include <functional>
#include <numeric>
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
  INITIAL = 0x31,  // Initial handshake
};

enum class ThirdByte : uint8_t {
  X10 = 0x10,
  X01 = 0x01,
};

enum class CategoryByte : uint8_t {
  INITIAL_REQUEST = 0x01,  // Initial Request
  STANDARD = 0x10,         // Standard data
  EXTRA = 0x21,            // Extra/extended data
};

static constexpr uint8_t STANDARD_PAYLOAD_LENGTH = 111;
static constexpr size_t RESPONSE_FRAME_SIZE = 203;

template<typename T> static uint8_t calculate_checksum(const T &data) {
  return std::accumulate(data.begin(), data.end(), uint8_t{});
}

using ResponseFrame = StaticVector<uint8_t, RESPONSE_FRAME_SIZE>;

class Parser {
 public:
  // Append a received byte; returns true when frame() holds a complete, valid response
  bool feed(uint8_t byte);

  const ResponseFrame &frame() const { return this->frame_; }
  CategoryByte category() const {
    return static_cast<CategoryByte>(this->frame_[static_cast<size_t>(ByteIndex::CATEGORY)]);
  }

 private:
  static bool is_expected_byte(size_t index, uint8_t byte);

  ResponseFrame frame_;
};

class Serializer {
 public:
  static std::vector<uint8_t> polling_message();
  static std::vector<uint8_t> polling_extra_message();
  static std::vector<uint8_t> initial_request();
  static std::vector<uint8_t> command_message(std::vector<uint8_t> &command_data);

 private:
  static std::vector<uint8_t> serialize_message(PreambleByte preamble, ThirdByte direction, CategoryByte category,
                                                size_t length);
  static std::vector<uint8_t> serialize_message(PreambleByte preamble, ThirdByte direction, CategoryByte category,
                                                std::vector<uint8_t> &data);
};
}  // namespace Protocol
}  // namespace panasonic_aquarea
}  // namespace esphome
