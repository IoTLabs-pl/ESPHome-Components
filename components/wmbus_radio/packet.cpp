#include "packet.h"

#include <ctime>

#include "esphome/core/alloc_helpers.h"
#include "esphome/core/helpers.h"
#include "esphome/components/wmbus_common/telegram.h"

#include "decode3of6.h"
#include "esphome/core/log.h"

// 3 bytes for mode C marks + len or first 3 bytes of mode T to decode into 2 bytes
#define WMBUS_PACKET_HEAD_SIZE (3)

// Mode C frame starts with \x54\xCD or \x54\x3D
#define WMBUS_MODE_C_MARK (0x54)
#define WMBUS_MODE_C_BLOCK_A_MARK (0xCD)
#define WMBUS_MODE_C_BLOCK_B_MARK (0x3D)
#define WMBUS_MODE_C_MARKS_LEN (2)

namespace esphome {
namespace wmbus_radio {
static const char *TAG = "wmbus";

const char *toString(LinkMode link_mode) {
  switch (link_mode) {
    case LinkMode::C1:
      return "C1";
    case LinkMode::T1:
      return "T1";
    default:
      return "";
  }
}

const char *toString(BlockType type) {
  switch (type) {
    case BlockType::A:
      return "A";
    case BlockType::B:
      return "B";
    default:
      return "";
  }
}

Packet::Packet() : data_(WMBUS_PACKET_HEAD_SIZE) {}

std::span<uint8_t> Packet::head() { return std::span(this->data_).first(WMBUS_PACKET_HEAD_SIZE); }

std::span<uint8_t> Packet::rest() {
  const auto head = this->head();

  uint8_t l_field;
  if (head[0] == WMBUS_MODE_C_MARK) {
    this->link_mode_ = LinkMode::C1;
    // C1 frame must have valid block type: A or B
    switch (head[1]) {
      case WMBUS_MODE_C_BLOCK_A_MARK:
        this->block_type_ = BlockType::A;
        break;
      case WMBUS_MODE_C_BLOCK_B_MARK:
        this->block_type_ = BlockType::B;
        break;
      default:
        return {};
    }
    l_field = head[WMBUS_MODE_C_MARKS_LEN];
  } else {
    // T1 frame has no block type
    this->link_mode_ = LinkMode::T1;
    const auto decoded = decode3of6(head);
    if (!decoded)
      return {};
    l_field = (*decoded)[0];
  }

  if (l_field == 0)
    return {};

  // Format A
  //   L-field = length without CRC fields and without L (1 byte)
  // Format B
  //   L-field = length with CRC fields and without L (1 byte)
  // The 2 first blocks contains 25 bytes when excluding CRC and the L-field
  // The other blocks contains 16 bytes when excluding the CRC-fields
  const size_t blocks = l_field < 26 ? 2 : (l_field - 26) / 16 + 3;
  const size_t frame_size = this->block_type_ == BlockType::B ? 1 + l_field : 1 + l_field + 2 * blocks;

  this->data_.resize(this->link_mode_ == LinkMode::C1 ? WMBUS_MODE_C_MARKS_LEN + frame_size : encoded_size(frame_size));
  return std::span(this->data_).subspan(WMBUS_PACKET_HEAD_SIZE);
}

void Packet::set_rssi(int8_t rssi) { this->rssi_ = rssi; }

std::optional<Frame> Packet::convert_to_frame() {
  ESP_LOGD(TAG, "Try to make frame from packet %s%s of size %zu", toString(this->link_mode_),
           toString(this->block_type_), this->data_.size());

  std::vector<uint8_t> data;
  if (this->link_mode_ == LinkMode::C1) {
    const auto frame = std::span(this->data_).subspan(WMBUS_MODE_C_MARKS_LEN);
    data.assign(frame.begin(), frame.end());
  } else {
    auto decoded = decode3of6(this->data_);
    if (!decoded)
      return {};
    data = std::move(*decoded);
  }

  // A failed CRC is a bit error: drop it before a meter reads a wrong value.
  if (!wmbus::remove_dll_crcs(data) || !wmbus::is_complete_frame(data)) {
    ESP_LOGD(TAG, "Dropped a packet of %zu bytes: bad CRC or not a wM-Bus frame", data.size());
    return {};
  }

  return Frame(std::move(data), this->link_mode_, this->block_type_, this->rssi_);
}

const std::vector<uint8_t> &Packet::get_raw_data() const { return data_; }

std::vector<uint8_t> &Frame::data() { return this->data_; }
LinkMode Frame::link_mode() { return this->link_mode_; }
BlockType Frame::block_type() { return this->block_type_; }
int8_t Frame::rssi() { return this->rssi_; }

std::vector<uint8_t> Frame::as_raw() { return this->data_; }
std::string Frame::as_hex() { return format_hex(this->data_); }
std::string Frame::as_rtlwmbus() {
  const size_t time_repr_size = sizeof("YYYY-MM-DD HH:MM:SS.00Z");
  char time_buffer[time_repr_size];
  auto t = std::time(NULL);
  std::strftime(time_buffer, time_repr_size, "%F %T.00Z", std::gmtime(&t));

  auto output = std::string{};
  output.reserve(2 + 5 + 24 + 1 + 4 + 5 + 2 * this->data_.size() + 1);

  output += toString(this->link_mode_);   // size 2
  output += ";1;1;";                      // size 5
  output += time_buffer;                  // size 24
  output += ';';                          // size 1
  output += std::to_string(this->rssi_);  // size up to 4
  output += ";;;0x";                      // size 5
  output += this->as_hex();               // size 2 * frame.size()
  output += "\n";                         // size 1

  return output;
}
std::string Frame::meter_id() {
  wmbus::Telegram telegram;
  if (!telegram.parse_envelope(this->data_))
    return "";
  return str_sprintf("%08x", (unsigned) telegram.sender().id);
}

void Frame::mark_as_handled() { this->frame_handlers_count_++; }
uint8_t Frame::frame_handlers_count() { return this->frame_handlers_count_; }

}  // namespace wmbus_radio
}  // namespace esphome
