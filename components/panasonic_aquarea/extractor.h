#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/optional.h"

#include "panasonic_aquarea.h"
#include "protocol.h"

namespace esphome {
namespace panasonic_aquarea {
// ==== Helper: Raw data extraction ====

class BitFieldReader {
 public:
  static uint8_t read_bits(const std::vector<uint8_t> &data, size_t byte, uint8_t bit, uint8_t bit_width) {
    // Bits are numbered from MSB=1 to LSB=8
    uint8_t mask = (1 << bit_width) - 1;
    uint8_t shift = 8 - (bit + bit_width - 1);
    return (data[byte] >> shift) & mask;
  }

  // TODO: done inside esphome/helpers.h?
  static uint32_t read_bytes(const std::vector<uint8_t> &data, size_t byte, uint8_t num_bytes) {
    uint32_t result = 0;
    for (int i = byte + num_bytes - 1; i >= static_cast<int>(byte); i--) {
      result = (result << 8) | data[i];
    }
    return result;
  }

  // TODO: done inside esphome/helpers.h?
  template<size_t N> static std::array<uint8_t, N> read_array(const std::vector<uint8_t> &data, size_t byte) {
    std::array<uint8_t, N> result;
    for (size_t i = 0; i < N; ++i) {
      result[i] = data[byte + i];
    }
    return result;
  }
};

// ==== Base interface for all extractors ====

template<typename T> class ExtractorInterface {
 public:
  virtual ~ExtractorInterface() = default;
  virtual optional<T> decode(const std::vector<uint8_t> &data) = 0;
  virtual void encode(std::vector<uint8_t> &data, const T &value) = 0;
};

// ==== Concrete extractors ====

class BinaryExtractor : public ExtractorInterface<bool> {
 public:
  BinaryExtractor(uint8_t byte, uint8_t bit) : byte_(byte), bit_(bit) {}

  optional<bool> decode(const std::vector<uint8_t> &data) override {
    if (data.size() <= byte_)
      return {};

    uint8_t raw = BitFieldReader::read_bits(data, byte_, bit_, 2);

    switch (raw) {
      case 0b00:
        ESP_LOGD(TAG, "no data for binary sensor (byte %u bit %u): %02x", byte_, bit_, data[byte_]);
        return {};
      case 0b11:
        ESP_LOGW(TAG, "invalid value 0b11 for binary sensor (byte %u bit %u): %02x", byte_, bit_, data[byte_]);
        return {};
      default:
        return raw == 0b10;
    }
  }

  void encode(std::vector<uint8_t> &data, const bool &value) override {
    if (data.size() <= byte_)
      return;

    constexpr uint8_t width = 2;
    const uint8_t shift = 8 - (bit_ + width - 1);
    const uint8_t mask = ((1u << width) - 1) << shift;
    const uint8_t raw = value ? 0b10 : 0b01;
    data[byte_] = (data[byte_] & ~mask) | (raw << shift);
  }

 private:
  static constexpr const char *TAG = "panasonic_aquarea.extractor.binary";
  uint8_t byte_;
  uint8_t bit_;
};

class FloatExtractor : public ExtractorInterface<float> {
 public:
  FloatExtractor(size_t byte, uint8_t bit_width, float offset, float multiplier)
      : byte_(byte), bit_width_(bit_width), offset_(offset), multiplier_(multiplier) {}

  optional<float> decode(const std::vector<uint8_t> &data) override {
    size_t required_size = byte_ + (bit_width_ / 8);
    if (data.size() < required_size)
      return {};

    uint32_t raw = BitFieldReader::read_bytes(data, byte_, bit_width_ / 8);
    return (raw + offset_) * multiplier_;
  }

  void encode(std::vector<uint8_t> &data, const float &value) override {
    size_t required_size = byte_ + (bit_width_ / 8);
    if (data.size() < required_size)
      return;

    uint32_t raw = static_cast<uint32_t>(value / multiplier_ - offset_);
    for (size_t i = 0; i < bit_width_ / 8; ++i) {
      data[byte_ + i] = (raw >> (8 * i)) & 0xFF;
    }
  }

 private:
  static constexpr const char *TAG = "panasonic_aquarea.extractor.float";
  size_t byte_;
  uint8_t bit_width_;
  float offset_;
  float multiplier_;
};

template<size_t N> class StringArrayExtractor : public ExtractorInterface<std::string> {
 public:
  StringArrayExtractor(size_t byte, uint8_t bit, uint8_t bit_width, const std::array<std::string, N> &strings)
      : byte_(byte), bit_(bit), bit_width_(bit_width), strings_(strings) {}

  optional<std::string> decode(const std::vector<uint8_t> &data) override {
    if (data.size() <= byte_)
      return {};

    auto raw_data = BitFieldReader::read_bits(data, byte_, bit_, bit_width_);

    if (raw_data == 0)
      return {};

    // -1 as indices are 1-based (zero means no data)
    uint8_t index = raw_data - 1;

    if (index >= strings_.size()) {
      ESP_LOGW(TAG, "invalid index %zu for string extractor (byte %zu bit %u)", index, byte_, bit_);
      return {};
    }
    return strings_[index];
  }

  void encode(std::vector<uint8_t> &data, const std::string &value) override {
    if (data.size() <= byte_)
      return;

    auto it = std::find(strings_.begin(), strings_.end(), value);
    if (it == strings_.end()) {
      ESP_LOGW(TAG, "string '%s' not found in extractor array (byte %zu bit %u)", value.c_str(), byte_, bit_);
      return;
    }

    size_t index = std::distance(strings_.begin(), it) + 1;  // +1 for 1-based index

    const uint8_t shift = 8 - (bit_ + bit_width_ - 1);
    const uint8_t mask = ((1u << bit_width_) - 1) << shift;
    data[byte_] = (data[byte_] & ~mask) | ((index << shift) & mask);
  }

 private:
  static constexpr const char *TAG = "panasonic_aquarea.extractor.stringarray";
  size_t byte_;
  uint8_t bit_;
  uint8_t bit_width_;
  std::array<std::string, N> strings_;
};

template<size_t KeyLen> class StringMapExtractor : public ExtractorInterface<std::string> {
 public:
  StringMapExtractor(size_t byte, uint8_t bit, uint8_t bit_width,
                     const std::map<std::array<uint8_t, KeyLen>, std::string> &strings)
      : byte_(byte), bit_(bit), bit_width_(bit_width), strings_(strings) {}

  optional<std::string> decode(const std::vector<uint8_t> &data) override {
    size_t required_size = byte_ + KeyLen;
    if (data.size() < required_size)
      return {};

    auto key = BitFieldReader::read_array<KeyLen>(data, byte_);

    // Calculate total bits in key and actual data range
    const size_t total_bits = 8 * KeyLen;
    const size_t data_end_bit = bit_ + bit_width_ - 1;

    // Mask left bits in first byte
    // we number bits from MSB=1 to LSB=8
    const uint8_t left_mask = 0xFF >> (bit_ - 1);
    key[0] &= left_mask;

    // Mask right bits in last byte
    const uint8_t right_mask = 0xFF << (total_bits - data_end_bit);
    key[KeyLen - 1] &= right_mask;

    auto it = strings_.find(key);
    if (it == strings_.end()) {
      ESP_LOGW(TAG, "key not found for string extractor (byte %zu, %zu bytes): %s", byte_, KeyLen,
               format_hex_pretty(key.data(), KeyLen).c_str());
      ESP_LOGW(TAG, "rmask: %x", right_mask);
      return {"undefined"};
    }

    return it->second;
  }

  void encode(std::vector<uint8_t> &data, const std::string &value) override {
    size_t required_size = byte_ + KeyLen;
    if (data.size() < required_size)
      return;

    auto it = std::find_if(
        strings_.begin(), strings_.end(),
        [&value](const std::pair<std::array<uint8_t, KeyLen>, std::string> &pair) { return pair.second == value; });
    if (it == strings_.end()) {
      ESP_LOGW(TAG, "string '%s' not found in extractor map (byte %zu)", value.c_str(), byte_);
      return;
    }

    const auto &key = it->first;

    // Calculate total bits in key and actual data range
    const size_t total_bits = 8 * KeyLen;
    const size_t data_end_bit = bit_ + bit_width_ - 1;

    // Mask left bits in first byte
    // we number bits from MSB=1 to LSB=8
    const uint8_t left_mask = 0xFF >> (bit_ - 1);
    data[byte_] = (data[byte_] & ~left_mask) | (key[0] & left_mask);

    // Mask right bits in last byte
    const uint8_t right_mask = 0xFF << (total_bits - data_end_bit);
    data[byte_ + KeyLen - 1] = (data[byte_ + KeyLen - 1] & ~right_mask) | (key[KeyLen - 1] & right_mask);

    // Copy middle bytes directly
    for (size_t i = 1; i < KeyLen - 1; ++i) {
      data[byte_ + i] = key[i];
    }
  }

 private:
  static constexpr const char *TAG = "panasonic_aquarea.extractor.stringmap";
  size_t byte_;
  uint8_t bit_;
  uint8_t bit_width_;
  std::map<std::array<uint8_t, KeyLen>, std::string> strings_;
};

template<typename T> class LambdaExtractor : public ExtractorInterface<T> {
 public:
  using Decoder = std::function<optional<T>(const std::vector<uint8_t> &)>;
  using Encoder = std::function<void(std::vector<uint8_t> &, const T &)>;

  LambdaExtractor(Decoder decoder, Encoder encoder = nullptr) : decoder_(decoder), encoder_(encoder) {}
  optional<T> decode(const std::vector<uint8_t> &data) override { return decoder_(data); }
  void encode(std::vector<uint8_t> &data, const T &value) override {
    if (encoder_)
      encoder_(data, value);
  }

 private:
  Decoder decoder_;
  Encoder encoder_;
};

// ==== CRTP mixin for entities ====

template<typename Derived, typename T> class ReadOnlyEntity : public panasonic_aquarea::ReadableEntity {
 public:
  void set_extractor(ExtractorInterface<T> *extractor) { extractor_ = extractor; }

  void handle_update(const std::vector<uint8_t> &data) override {
    auto value = extractor_->decode(data);

    if (!value.has_value() || publish_dedup_.next(*value) == false) {
      auto name = static_cast<Derived *>(this)->get_name();
      ESP_LOGV("ReadableEntity", "Value unchanged for %s, not publishing", name.c_str());

      return;
    }
    static_cast<Derived *>(this)->publish_state(*value);
  }

 protected:
  ExtractorInterface<T> *extractor_;
  Deduplicator<T> publish_dedup_;
};

template<typename Derived, typename T> class WriteOnlyEntity : public panasonic_aquarea::WritableEntity {
 public:
  void set_extractor(ExtractorInterface<T> *extractor) { extractor_ = extractor; }

  void send_command(const T &value) { this->extractor_->encode(this->parent_->get_command_data(), value); }

 protected:
  ExtractorInterface<T> *extractor_;
};

template<typename Derived, typename T>
class ReadWriteEntity : public ReadOnlyEntity<Derived, T>, public WriteOnlyEntity<Derived, T> {
 public:
  void set_extractor(ExtractorInterface<T> *extractor) {
    ReadOnlyEntity<Derived, T>::set_extractor(extractor);
    WriteOnlyEntity<Derived, T>::set_extractor(extractor);
  }
};
}  // namespace panasonic_aquarea
}  // namespace esphome