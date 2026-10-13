#include "packet.h"

#include <cstring>
#include <ctime>

#include "decode3of6.h"
#include "esphome/components/wmbus_common/telegram.h"
#include "esphome/core/alloc_helpers.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

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

bool FrameFormat::crc_ok(std::span<const uint8_t> data, std::span<const uint8_t, 2> crc_bytes) {
  uint16_t calc = esphome::crc16be(data.data(), data.size(), 0x0000, 0x3D65, false, true);
  uint16_t check = esphome::encode_uint16(crc_bytes[0], crc_bytes[1]);
  return calc == check;
}

bool FrameFormat::is_complete(std::span<const uint8_t> data) const {
  return data.size() >= 11 && (data[1] == 0x44 || data[1] == 0x46);
}

namespace {

class FrameFormatA final : public FrameFormat {
 public:
  BlockType type() const override { return BlockType::A; }

  size_t frame_size(uint8_t l_field) const override {
    if (l_field == 0)
      return 0;
    const size_t nr_blocks = l_field < 26 ? 2 : (l_field - 26) / 16 + 3;
    return 1 + l_field + 2 * nr_blocks;
  }

  bool strip_crcs(std::vector<uint8_t> &payload) const override {
    const std::span<const uint8_t> bytes{payload};
    const size_t len = bytes.size();
    if (len < 12)
      return false;
    if (!crc_ok(bytes.first(10), bytes.subspan(10).first<2>()))
      return false;

    size_t pos = 12;
    for (; pos + 18 <= len; pos += 18) {
      if (!crc_ok(bytes.subspan(pos, 16), bytes.subspan(pos + 16).first<2>()))
        return false;
    }
    const size_t tail = pos + 2 < len ? len - 2 - pos : 0;
    if (tail > 0 && !crc_ok(bytes.subspan(pos, tail), bytes.subspan(pos + tail).first<2>()))
      return false;

    size_t out = 10;
    for (pos = 12; pos + 18 <= len; pos += 18) {
      memmove(payload.data() + out, payload.data() + pos, 16);
      out += 16;
    }
    memmove(payload.data() + out, payload.data() + pos, tail);
    out += tail;
    payload.resize(out);
    payload[0] = static_cast<uint8_t>(out - 1);
    return true;
  }
};

class FrameFormatB final : public FrameFormat {
 public:
  BlockType type() const override { return BlockType::B; }

  size_t frame_size(uint8_t l_field) const override {
    if (l_field == 0)
      return 0;
    return 1 + l_field;
  }

  bool strip_crcs(std::vector<uint8_t> &payload) const override {
    const std::span<const uint8_t> bytes{payload};
    const size_t len = bytes.size();
    if (len < 12)
      return false;

    size_t crc1_pos, crc2_pos;
    if (len <= 128) {
      crc1_pos = len - 2;
      crc2_pos = 0;
    } else {
      crc1_pos = 126;
      crc2_pos = len - 2;
    }

    if (!crc_ok(bytes.first(crc1_pos), bytes.subspan(crc1_pos).first<2>()))
      return false;
    size_t out = crc1_pos;
    if (crc2_pos > 0) {
      const size_t from2 = crc1_pos + 2;
      if (!crc_ok(bytes.subspan(from2, crc2_pos - from2), bytes.subspan(crc2_pos).first<2>()))
        return false;
      memmove(payload.data() + out, payload.data() + from2, crc2_pos - from2);
      out += crc2_pos - from2;
    }
    payload.resize(out);
    payload[0] = static_cast<uint8_t>(out - 1);
    return true;
  }
};

class ModeC1Handler final : public LinkModeHandler {
 public:
  static constexpr uint8_t MARK = 0x54;
  static constexpr uint8_t BLOCK_A_MARK = 0xCD;
  static constexpr uint8_t BLOCK_B_MARK = 0x3D;
  static constexpr size_t MARKS_LEN = 2;

  LinkMode mode() const override { return LinkMode::C1; }

  BlockType detect_block_type(std::span<const uint8_t> head) const override {
    if (head.size() >= MARKS_LEN && head[0] == MARK) {
      if (head[1] == BLOCK_A_MARK)
        return BlockType::A;
      if (head[1] == BLOCK_B_MARK)
        return BlockType::B;
    }
    return BlockType::UNKNOWN;
  }

  uint8_t extract_l_field(std::span<const uint8_t> head) const override {
    return head.size() > MARKS_LEN ? head[MARKS_LEN] : 0;
  }

  size_t wire_size(size_t frame_size) const override { return MARKS_LEN + frame_size; }

  bool decode_payload(std::vector<uint8_t> &data) const override {
    if (data.size() < MARKS_LEN)
      return false;
    data.erase(data.begin(), data.begin() + MARKS_LEN);
    return true;
  }
};

class ModeT1Handler final : public LinkModeHandler {
 public:
  LinkMode mode() const override { return LinkMode::T1; }

  BlockType detect_block_type(std::span<const uint8_t> /*head*/) const override { return BlockType::A; }

  uint8_t extract_l_field(std::span<const uint8_t> head) const override {
    auto decoded = decode3of6(head);
    return (decoded && !decoded->empty()) ? (*decoded)[0] : 0;
  }

  size_t wire_size(size_t frame_size) const override { return encoded_size(frame_size); }

  bool decode_payload(std::vector<uint8_t> &data) const override {
    auto decoded = decode3of6(data);
    if (!decoded)
      return false;
    data = std::move(*decoded);
    return true;
  }
};

}  // namespace

const FrameFormat *FrameFormat::create(BlockType type) {
  switch (type) {
    case BlockType::A: {
      static const FrameFormatA format_a;
      return &format_a;
    }
    case BlockType::B: {
      static const FrameFormatB format_b;
      return &format_b;
    }
    default:
      return nullptr;
  }
}

const LinkModeHandler *LinkModeHandler::create(std::span<const uint8_t> initial_bytes) {
  if (initial_bytes.empty())
    return nullptr;
  if (initial_bytes[0] == ModeC1Handler::MARK) {
    static const ModeC1Handler c1_handler;
    return &c1_handler;
  }
  static const ModeT1Handler t1_handler;
  return &t1_handler;
}

Packet::Packet() : data_(HEAD_SIZE) {}

std::span<uint8_t> Packet::head() { return std::span(this->data_).first(HEAD_SIZE); }

std::span<uint8_t> Packet::rest() {
  const auto head = this->head();

  this->mode_handler_ = LinkModeHandler::create(head);
  if (!this->mode_handler_)
    return {};

  this->link_mode_ = this->mode_handler_->mode();
  this->block_type_ = this->mode_handler_->detect_block_type(head);
  if (this->block_type_ == BlockType::UNKNOWN)
    return {};

  this->format_ = FrameFormat::create(this->block_type_);
  if (!this->format_)
    return {};

  const uint8_t l_field = this->mode_handler_->extract_l_field(head);
  if (l_field == 0)
    return {};

  const size_t frame_bytes = this->format_->frame_size(l_field);
  const size_t total_wire_bytes = this->mode_handler_->wire_size(frame_bytes);
  if (total_wire_bytes < HEAD_SIZE)
    return {};

  this->data_.resize(total_wire_bytes);
  return std::span(this->data_).subspan(HEAD_SIZE);
}

void Packet::set_rssi(int8_t rssi) { this->rssi_ = rssi; }

std::optional<Frame> Packet::convert_to_frame() {
  ESP_LOGD(TAG, "Try to make frame from packet %s%s of size %zu", toString(this->link_mode_),
           toString(this->block_type_), this->data_.size());

  if (!this->mode_handler_ || !this->format_)
    return {};

  if (!this->mode_handler_->decode_payload(this->data_)) {
    ESP_LOGD(TAG, "Dropped a packet of %zu bytes: decode payload failed", this->data_.size());
    return {};
  }

  if (!this->format_->strip_crcs(this->data_)) {
    ESP_LOGD(TAG, "Dropped a packet of %zu bytes: bad CRC", this->data_.size());
    return {};
  }

  if (!this->format_->is_complete(this->data_)) {
    ESP_LOGD(TAG, "Dropped a packet of %zu bytes: not a complete wM-Bus frame", this->data_.size());
    return {};
  }

  return Frame(std::move(this->data_), this->link_mode_, this->block_type_, this->rssi_);
}

const std::vector<uint8_t> &Packet::get_raw_data() const { return this->data_; }

std::vector<uint8_t> &Frame::data() { return this->data_; }
LinkMode Frame::link_mode() { return this->link_mode_; }
BlockType Frame::block_type() { return this->block_type_; }
int8_t Frame::rssi() { return this->rssi_; }

std::span<const uint8_t> Frame::as_raw() const { return this->data_; }
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
