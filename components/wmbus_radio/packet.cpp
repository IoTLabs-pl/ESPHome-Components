#include "packet.h"

#include <ctime>

#include "esphome/core/helpers.h"
#include "esphome/components/wmbus_common/meters.h"

#include "decode3of6.h"

// 3 bytes for mode C marks + len or first 3 bytes of mode T to decode into 2 bytes
#define WMBUS_FRAME_PRELOAD_SIZE (3)

// Mode C frame starts with \x54\xCD or \x54\x3D
#define WMBUS_MODE_C_MARK (0x54)
#define WMBUS_MODE_C_BLOCK_A_MARK (0xCD)
#define WMBUS_MODE_C_BLOCK_B_MARK (0x3D)
#define WMBUS_MODE_C_MARKS_LEN (2)

namespace esphome {
namespace wmbus_radio {
static const char *TAG = "wmbus";

const char *toString(BlockType type) {
  switch (type) {
    case BlockType::A:
      return "a";
    case BlockType::B:
      return "b";
    default:
      return "";
  }
}

Packet::Packet() { this->data_.reserve(WMBUS_FRAME_PRELOAD_SIZE); }

// Determine the link mode based on the first byte of the data
LinkMode Packet::link_mode() {
  if (this->link_mode_ == LinkMode::UNKNOWN)
    if (this->data_.size())
      if (this->data_[0] == WMBUS_MODE_C_MARK)
        this->link_mode_ = LinkMode::C1;
      else
        this->link_mode_ = LinkMode::T1;

  return this->link_mode_;
}

// Determine block type for C1 mode based on second byte of data
BlockType Packet::block_type() {
  if (this->block_type_ == BlockType::UNKNOWN) {
    if (this->link_mode() == LinkMode::C1) {
      switch (this->data_[1]) {
        case WMBUS_MODE_C_BLOCK_A_MARK:
          this->block_type_ = BlockType::A;
          break;
        case WMBUS_MODE_C_BLOCK_B_MARK:
          this->block_type_ = BlockType::B;
          break;
        default:
          break;
      }
    }
  }

  return this->block_type_;
}

void Packet::set_rssi(int8_t rssi) { this->rssi_ = rssi; }

// Get value of L-field
uint8_t Packet::l_field() {
  switch (this->link_mode()) {
    case LinkMode::C1:
      return this->data_[0];
    case LinkMode::T1: {
      auto decoded = decode3of6(this->data_);
      if (decoded)
        return (*decoded)[0];
    }
    default:
      break;
  }
  return 0;
}

size_t Packet::expected_size() {
  if (!this->expected_size_) {
    // Format A
    //   L-field = length without CRC fields and without L (1 byte)
    // Format B
    //   L-field = length with CRC fields and without L (1 byte)

    auto l_field = this->l_field();

    if (l_field == 0) {
      return 0;
    }

    // The 2 first blocks contains 25 bytes when excluding CRC and the L-field
    // The other blocks contains 16 bytes when excluding the CRC-fields
    // Less than 26 (15 + 10)
    auto nrBlocks = l_field < 26 ? 2 : (l_field - 26) / 16 + 3;

    // Add all extra fields, excluding the CRC fields + 2 CRC bytes for each block
    auto nrBytes = l_field + 1 + 2 * nrBlocks;

    switch (this->link_mode()) {
      case LinkMode::C1:
        switch (this->block_type()) {
          case BlockType::A:
            this->expected_size_ = nrBytes;
            break;
          case BlockType::B:
            this->expected_size_ = 1 + l_field;
            break;
          default:
            break;
        }
        break;
      case LinkMode::T1:
        this->expected_size_ = encoded_size(nrBytes);
        break;
      default:
        break;
    }
  }
  return this->expected_size_;
}

size_t Packet::rx_capacity() {
  // TODO: Remove side effects?
  auto cap = this->data_.capacity() - this->data_.size();
  this->data_.resize(this->data_.capacity());
  return cap;
}

uint8_t *Packet::rx_data_ptr() { return this->data_.data() + this->data_.size(); }

bool Packet::calculate_payload_size() {
  auto total_length = this->expected_size();
  this->data_.reserve(total_length);
  return total_length;
}

bool Packet::validate_preamble() {
  bool is_preamble_valid = false;

  switch (this->link_mode()) {
    case LinkMode::C1:
      // C1 frame must have valid block type: A or B
      if (this->block_type() == BlockType::A || this->block_type() == BlockType::B) {
        is_preamble_valid = true;
      }
      break;
    case LinkMode::T1:
      // T1 frame has no block type
      is_preamble_valid = true;
      break;
    default:
      break;
  }

  if (is_preamble_valid) {
    this->trim_preamble();
  }

  return is_preamble_valid;
}

void Packet::trim_preamble() {
  switch (this->link_mode()) {
    case LinkMode::C1:
      this->data_.erase(this->data_.begin(), this->data_.begin() + 2);
      break;
    default:
      break;
  }
}

std::optional<Frame> Packet::convert_to_frame() {
  ESP_LOGD(TAG, "Try to make frame from packet %s%s of size %u", toString(this->link_mode()),
           toString(this->block_type()), this->expected_size());

  std::optional<Frame> frame = {};

  if (this->link_mode() == LinkMode::T1 && this->expected_size() == this->data_.size()) {
    auto decoded_data = decode3of6(this->data_);
    if (decoded_data)
      this->data_ = decoded_data.value();
  }

  removeAnyDLLCRCs(this->data_);
  int dummy;
  if (checkWMBusFrame(this->data_, (size_t *) &dummy, &dummy, &dummy, false) == FrameStatus::FullFrame)
    frame.emplace(this);

  delete this;

  return frame;
}

const std::vector<uint8_t> &Packet::get_raw_data() const { return data_; }

Frame::Frame(Packet *packet)
    : data_(std::move(packet->data_)),
      link_mode_(packet->link_mode_),
      block_type_(packet->block_type_),
      rssi_(packet->rssi_) {}

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

  output += linkModeName(this->link_mode_);  // size 2
  output += ";1;1;";                         // size 5
  output += time_buffer;                     // size 24
  output += ';';                             // size 1
  output += std::to_string(this->rssi_);     // size up to 4
  output += ";;;0x";                         // size 5
  output += this->as_hex();                  // size 2 * frame.size()
  output += "\n";                            // size 1

  return output;
}
std::string Frame::meter_id() {
  Telegram telegram;
  telegram.parseWMBUSHeader(this->data_);
  return telegram.addresses[0].str();
}

void Frame::mark_as_handled() { this->frame_handlers_count_++; }
uint8_t Frame::frame_handlers_count() { return this->frame_handlers_count_; }

}  // namespace wmbus_radio
}  // namespace esphome