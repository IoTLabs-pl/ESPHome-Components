#include "protocol.h"

namespace esphome {
namespace panasonic_aquarea {
namespace Protocol {
// ---- Serializer implementation ----
RequestFrame Serializer::serialize_message(PreambleByte preamble, CategoryByte category, RequestFrame frame) {
  // Expect full, zero-filled frame of proper size.
  // Layout: PREAMBLE(0) LENGTH(1) DIRECTION(2) CATEGORY(3) PAYLOAD(...) CHECKSUM(last)
  frame[static_cast<size_t>(ByteIndex::PREAMBLE)] = static_cast<uint8_t>(preamble);
  frame[static_cast<size_t>(ByteIndex::PAYLOAD_LENGTH)] = frame.size() - 3;  // Exclude PREAMBLE, LENGTH, CHECKSUM
  frame[static_cast<size_t>(ByteIndex::DIRECTION)] = static_cast<uint8_t>(ThirdByte::X01);
  frame[static_cast<size_t>(ByteIndex::CATEGORY)] = static_cast<uint8_t>(category);

  // Compute and write checksum
  frame.back() = 0u - calculate_checksum(frame);

  return frame;
}

RequestFrame Serializer::polling_message() {
  // Message format: 0x71, 0x6C, 0x01, 0x10, ... (111 bytes total)
  return serialize_message(PreambleByte::POLLING, CategoryByte::STANDARD, RequestFrame{});
}

RequestFrame Serializer::polling_extra_message() {
  // Message format: 0x71, 0x6C, 0x01, 0x21, ... (111 bytes total)
  return serialize_message(PreambleByte::POLLING, CategoryByte::EXTRA, RequestFrame{});
}

RequestFrame Serializer::command_message(RequestFrame command_data) {
  // Message format: 0xF1, 0x6C, 0x01, 0x10, ... (111 bytes total)
  return serialize_message(PreambleByte::COMMAND, CategoryByte::STANDARD, command_data);
}
}  // namespace Protocol

}  // namespace panasonic_aquarea
}  // namespace esphome
