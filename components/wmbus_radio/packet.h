#pragma once
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "esphome/core/helpers.h"

namespace esphome {
namespace wmbus_radio {

enum class LinkMode { UNKNOWN = 0, C1, T1 };
enum class BlockType { UNKNOWN = 0, A, B };

const char *toString(LinkMode link_mode);
const char *toString(BlockType type);

class FrameFormat {
 public:
  virtual ~FrameFormat() = default;
  virtual BlockType type() const = 0;
  virtual size_t frame_size(uint8_t l_field) const = 0;
  virtual bool strip_crcs(std::vector<uint8_t> &payload) const = 0;
  virtual bool is_complete(std::span<const uint8_t> data) const;

  static const FrameFormat *create(BlockType type);

 protected:
  static bool crc_ok(std::span<const uint8_t> data, std::span<const uint8_t, 2> crc_bytes);
};

class LinkModeHandler {
 public:
  virtual ~LinkModeHandler() = default;
  virtual LinkMode mode() const = 0;
  virtual BlockType detect_block_type(std::span<const uint8_t> head) const = 0;
  virtual uint8_t extract_l_field(std::span<const uint8_t> head) const = 0;
  virtual size_t wire_size(size_t frame_size) const = 0;
  virtual bool decode_payload(std::vector<uint8_t> &data) const = 0;

  static const LinkModeHandler *create(std::span<const uint8_t> initial_bytes);
};

struct Frame;

struct Packet {
 public:
  static constexpr size_t HEAD_SIZE = 3;

  Packet();

  std::span<uint8_t> head();
  // Empty when the head does not start a valid packet
  std::span<uint8_t> rest();
  void set_rssi(int8_t rssi);

  std::optional<Frame> convert_to_frame();
  const std::vector<uint8_t> &get_raw_data() const;

  LinkMode link_mode() const { return this->link_mode_; }
  BlockType block_type() const { return this->block_type_; }

 protected:
  std::vector<uint8_t> data_;
  LinkMode link_mode_ = LinkMode::UNKNOWN;
  BlockType block_type_ = BlockType::UNKNOWN;
  const LinkModeHandler *mode_handler_{nullptr};
  const FrameFormat *format_{nullptr};
  int8_t rssi_{0};
};

struct Frame {
 public:
  Frame(std::vector<uint8_t> data, LinkMode lm, BlockType bt, int8_t rssi)
      : data_(std::move(data)), link_mode_(lm), block_type_(bt), rssi_(rssi){};

  std::vector<uint8_t> &data();
  LinkMode link_mode();
  BlockType block_type();
  int8_t rssi();

  std::span<const uint8_t> as_raw() const;
  std::string as_hex();
  std::string as_rtlwmbus();
  std::string meter_id();

  void mark_as_handled();
  uint8_t frame_handlers_count();

 protected:
  std::vector<uint8_t> data_;
  LinkMode link_mode_;
  BlockType block_type_;
  int8_t rssi_;
  uint8_t frame_handlers_count_ = 0;
};

}  // namespace wmbus_radio
}  // namespace esphome
