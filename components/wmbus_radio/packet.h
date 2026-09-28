#pragma once
#include <cstdint>
#include <cstddef>
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

struct Frame;

struct Packet {
 public:
  Packet();

  std::span<uint8_t> head();
  // Empty when the head does not start a valid packet
  std::span<uint8_t> rest();
  void set_rssi(int8_t rssi);

  std::optional<Frame> convert_to_frame();
  const std::vector<uint8_t> &get_raw_data() const;

 protected:
  std::vector<uint8_t> data_;
  LinkMode link_mode_ = LinkMode::UNKNOWN;
  BlockType block_type_ = BlockType::UNKNOWN;
  int8_t rssi_;
};

struct Frame {
 public:
  Frame(std::vector<uint8_t> data, LinkMode lm, BlockType bt, int8_t rssi)
      : data_(std::move(data)), link_mode_(lm), block_type_(bt), rssi_(rssi){};

  std::vector<uint8_t> &data();
  LinkMode link_mode();
  BlockType block_type();
  int8_t rssi();

  std::vector<uint8_t> as_raw();
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
