#include "protocol.h"
#include "panasonic_aquarea.h"

namespace esphome {
namespace panasonic_aquarea {
namespace Protocol {
static const char *const TAG = "panasonic_aquarea.protocol";

bool Parser::is_expected_byte(size_t index, uint8_t byte) {
  // Frame structure: preamble (1) + length (1) + direction+category (2) + payload (N) + checksum (1)
  // Length field encodes (direction+category+payload) = (2 + N), so total frame size = length + 3
  switch (static_cast<ByteIndex>(index)) {
    case ByteIndex::PREAMBLE:
      return byte == static_cast<uint8_t>(PreambleByte::POLLING);
    case ByteIndex::PAYLOAD_LENGTH:
      return byte + 3u == RESPONSE_FRAME_SIZE;
    case ByteIndex::DIRECTION:
      return byte == static_cast<uint8_t>(ThirdByte::X01);
    case ByteIndex::CATEGORY:
      return byte == static_cast<uint8_t>(CategoryByte::STANDARD) || byte == static_cast<uint8_t>(CategoryByte::EXTRA);
    default:
      return true;
  }
}

bool Parser::feed(uint8_t byte) {
  if (this->frame_.size() == RESPONSE_FRAME_SIZE)
    this->frame_.clear();  // Previous frame was already handed out

  // Broken frame is dropped whole; resync on the next preamble costs at most one more frame
  if (!is_expected_byte(this->frame_.size(), byte)) {
    this->frame_.clear();
    if (!is_expected_byte(0, byte))
      return false;
  }

  this->frame_.push_back(byte);
  if (this->frame_.size() < RESPONSE_FRAME_SIZE)
    return false;

  if (calculate_checksum(this->frame_) == 0)
    return true;

  ESP_LOGD(TAG, "Checksum mismatch, dropping frame");
  this->frame_.clear();
  return false;
}

// ---- Serializer implementation ----
std::vector<uint8_t> Serializer::serialize_message(PreambleByte preamble, ThirdByte direction, CategoryByte category,
                                                   size_t length) {
  std::vector<uint8_t> frame(length, 0);
  // Delegate to rvalue overload that mutates in place
  return serialize_message(preamble, direction, category, frame);
}

std::vector<uint8_t> Serializer::serialize_message(PreambleByte preamble, ThirdByte direction, CategoryByte category,
                                                   std::vector<uint8_t> &frame) {
  // Expect full, zero-filled frame of proper size.
  // Layout: PREAMBLE(0) LENGTH(1) DIRECTION(2) CATEGORY(3) PAYLOAD(...) CHECKSUM(last)
  const size_t total_size = frame.size();
  const size_t payload_length = total_size - 3;  // Exclude PREAMBLE, LENGTH, CHECKSUM

  frame[static_cast<size_t>(ByteIndex::PREAMBLE)] = static_cast<uint8_t>(preamble);
  frame[static_cast<size_t>(ByteIndex::PAYLOAD_LENGTH)] = static_cast<uint8_t>(payload_length);
  frame[static_cast<size_t>(ByteIndex::DIRECTION)] = static_cast<uint8_t>(direction);
  frame[static_cast<size_t>(ByteIndex::CATEGORY)] = static_cast<uint8_t>(category);

  // Compute and write checksum
  frame[total_size - 1] = 0u - calculate_checksum(frame);

  return frame;
}

std::vector<uint8_t> Serializer::polling_message() {
  // Message format: 0x71, 0x6C, 0x01, 0x10, ... (111 bytes total)
  return serialize_message(PreambleByte::POLLING, ThirdByte::X01, CategoryByte::STANDARD, STANDARD_PAYLOAD_LENGTH);
}

std::vector<uint8_t> Serializer::polling_extra_message() {
  // Message format: 0x71, 0x6C, 0x01, 0x21, ... (111 bytes total)
  return serialize_message(PreambleByte::POLLING, ThirdByte::X01, CategoryByte::EXTRA, STANDARD_PAYLOAD_LENGTH);
}

std::vector<uint8_t> Serializer::initial_request() {
  // Message format: 0x31, 0x05, 0x10, 0x01, ... (8 bytes total)
  return serialize_message(PreambleByte::INITIAL, ThirdByte::X10, CategoryByte::INITIAL_REQUEST, 8);
}

std::vector<uint8_t> Serializer::command_message(std::vector<uint8_t> &command) {
  // Message format: 0xF1, 0x6C, 0x01, 0x10, ... (111 bytes total)
  return serialize_message(PreambleByte::COMMAND, ThirdByte::X01, CategoryByte::STANDARD, command);
}
}  // namespace Protocol

}  // namespace panasonic_aquarea
}  // namespace esphome
