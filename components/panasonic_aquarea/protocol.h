#pragma once

#include "ring_buffer.h"
#include <forward_list>
#include <vector>
#include <array>
#include <cstring>
#include <cstdint>
#include <functional>
#include <numeric>
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
  UNKNOWN = 0xFF,
};

enum class ThirdByte : uint8_t {
  X10 = 0x10,
  X01 = 0x01,
  UNKNOWN = 0xFF,
};

enum class CategoryByte : uint8_t {
  INITIAL_REQUEST = 0x01,  // Initial Request
  STANDARD = 0x10,         // Standard data
  EXTRA = 0x21,            // Extra/extended data
  UNKNOWN = 0xFF,
};

static constexpr uint8_t STANDARD_PAYLOAD_LENGTH = 111;

template<typename T> static uint8_t calculate_checksum(const T &data) {
  return std::accumulate(data.begin(), data.end(), uint8_t{});
}

class Parser {
 private:
  struct Response {
    std::vector<uint8_t> data;
    CategoryByte category;
  };

  static bool find_and_align_to_preamble(ResponseBuffer &buffer);
  static size_t validate_frame_header(const ResponseBuffer &buffer);
  static bool validate_frame_checksum(const std::vector<uint8_t> &buffer);
  static CategoryByte validate_response_type(const std::vector<uint8_t> &buffer);

 public:
  // Parse and extract a complete frame from buffer
  // Returns parsed response with data, or empty vector if no valid frame
  // Automatically removes processed/invalid bytes from buffer
  static Response parse_response(ResponseBuffer &buffer);
};

class Serializer {
 public:
  static std::vector<uint8_t> polling_message();
  static std::vector<uint8_t> polling_extra_message();
  static std::vector<uint8_t> initial_request();
  static std::vector<uint8_t> command_message(std::vector<uint8_t> &command_data);

 private:
  static std::vector<uint8_t> serialize_message(PreambleByte preamble, ThirdByte direction, CategoryByte category,
                                                size_t payload_length);
  static std::vector<uint8_t> serialize_message(PreambleByte preamble, ThirdByte direction, CategoryByte category,
                                                std::vector<uint8_t> &data);
};
}  // namespace Protocol
}  // namespace panasonic_aquarea
}  // namespace esphome
