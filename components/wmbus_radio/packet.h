#pragma once
#include <cstdint>
#include <cstddef>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "esphome/core/helpers.h"
#include "esphome/components/wmbus_common/wmbus.h"

namespace esphome {
namespace wmbus_radio {

enum class BlockType { UNKNOWN = 0, A = 1, B = 2 };

const char *toString(BlockType type);

struct Frame;

struct Packet {
  friend class Frame;

 public:
  Packet();

  uint8_t *rx_data_ptr();
  size_t rx_capacity();
  bool calculate_payload_size();
  void set_rssi(int8_t rssi);
  bool validate_preamble();

  std::optional<Frame> convert_to_frame();
  const std::vector<uint8_t> &get_raw_data() const;

 protected:
  std::vector<uint8_t> data_;

  size_t expected_size_ = 0;

  void trim_preamble();

  uint8_t l_field();
  int8_t rssi_ = 0;

  BlockType block_type();
  LinkMode link_mode();
  size_t expected_size();

  LinkMode link_mode_ = LinkMode::UNKNOWN;
  BlockType block_type_ = BlockType::UNKNOWN;
};

struct Frame {
 public:
  Frame(Packet *packet);
  Frame(std::vector<uint8_t> data, LinkMode lm, BlockType bt, int8_t rssi)
      : data_(std::move(data)), link_mode_(lm), block_type_(bt), rssi_(rssi) {};

  std::vector<uint8_t> &data();
  LinkMode link_mode();
  BlockType block_type();
  int8_t rssi();

  std::vector<uint8_t> as_raw();
  std::string as_hex();
  std::string as_rtlwmbus();
  std::string meter_id();

  void mark_as_handled();
  uint8_t handlers_count();

 protected:
  std::vector<uint8_t> data_;
  LinkMode link_mode_;
  BlockType block_type_;
  int8_t rssi_;
  uint8_t handlers_count_ = 0;
};

}  // namespace wmbus_radio
}  // namespace esphome
