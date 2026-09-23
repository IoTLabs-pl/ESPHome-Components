#pragma once

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/optional.h"

namespace esphome {
namespace panasonic_aquarea {

// ==== Base entity classes ====

class ReadableEntity {
 public:
  virtual ~ReadableEntity() = default;
  virtual void handle_update(std::span<const uint8_t> data) = 0;
};

class WritableEntity {
 public:
  virtual ~WritableEntity() = default;
};

// ==== Base for extractors of a bit field within one byte ====

class BitField {
 protected:
  BitField(size_t byte, uint8_t bit, uint8_t bit_width) : byte_(byte), bit_(bit), bit_width_(bit_width) {}

  uint8_t read_bits(std::span<const uint8_t> data) const {
    // Bits are numbered from MSB=1 to LSB=8
    uint8_t mask = (1 << bit_width_) - 1;
    uint8_t shift = 8 - (bit_ + bit_width_ - 1);
    return (data[byte_] >> shift) & mask;
  }

  void write_bits(std::span<uint8_t> data, uint8_t value) const {
    uint8_t mask = (1 << bit_width_) - 1;
    uint8_t shift = 8 - (bit_ + bit_width_ - 1);
    data[byte_] = (data[byte_] & ~(mask << shift)) | ((value & mask) << shift);
  }

  size_t byte_;
  uint8_t bit_;
  uint8_t bit_width_;
};

// ==== Base interface for all extractors ====

template<typename T> class ExtractorInterface {
 public:
  virtual ~ExtractorInterface() = default;
  virtual optional<T> decode(std::span<const uint8_t> data) = 0;
  virtual void encode(std::span<uint8_t> data, const T &value) = 0;
};

// ==== Concrete extractors ====

class BinaryExtractor : public ExtractorInterface<bool>, protected BitField {
 public:
  // bit_width 2: state field (0b01 = false, 0b10 = true), bit_width 1: single-bit flag (1 = true)
  BinaryExtractor(size_t byte, uint8_t bit, uint8_t bit_width = 2) : BitField(byte, bit, bit_width) {}

  optional<bool> decode(std::span<const uint8_t> data) override {
    uint8_t raw = read_bits(data);

    if (bit_width_ == 1)
      return raw == 1;

    switch (raw) {
      case 0b00:
        ESP_LOGD(TAG, "no data for binary sensor (byte %zu bit %u): %02x", byte_, bit_, data[byte_]);
        return {};
      case 0b11:
        ESP_LOGW(TAG, "invalid value 0b11 for binary sensor (byte %zu bit %u): %02x", byte_, bit_, data[byte_]);
        return {};
      default:
        return raw == 0b10;
    }
  }

  void encode(std::span<uint8_t> data, const bool &value) override {
    write_bits(data, bit_width_ == 1 ? value : (value ? 0b10 : 0b01));
  }

 private:
  static constexpr const char *TAG = "panasonic_aquarea.extractor.binary";
};

class FloatExtractor : public ExtractorInterface<float> {
 public:
  FloatExtractor(size_t byte, uint8_t bit_width, float offset, float multiplier)
      : byte_(byte), bit_width_(bit_width), offset_(offset), multiplier_(multiplier) {}

  optional<float> decode(std::span<const uint8_t> data) override {
    uint32_t raw = 0;
    for (size_t i = 0; i < bit_width_ / 8; ++i) {
      raw |= static_cast<uint32_t>(data[byte_ + i]) << (8 * i);
    }
    return (raw + offset_) * multiplier_;
  }

  void encode(std::span<uint8_t> data, const float &value) override {
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

template<size_t N> class StringArrayExtractor : public ExtractorInterface<std::string>, protected BitField {
 public:
  StringArrayExtractor(size_t byte, uint8_t bit, uint8_t bit_width, const std::array<std::string, N> &strings)
      : BitField(byte, bit, bit_width), strings_(strings) {}

  optional<std::string> decode(std::span<const uint8_t> data) override {
    auto raw_data = read_bits(data);

    if (raw_data == 0)
      return {};

    // -1 as indices are 1-based (zero means no data)
    uint8_t index = raw_data - 1;

    if (index >= strings_.size()) {
      ESP_LOGW(TAG, "invalid index %u for string extractor (byte %zu bit %u)", index, byte_, bit_);
      return {};
    }
    return strings_[index];
  }

  void encode(std::span<uint8_t> data, const std::string &value) override {
    auto it = std::find(strings_.begin(), strings_.end(), value);
    if (it == strings_.end()) {
      ESP_LOGW(TAG, "string '%s' not found in extractor array (byte %zu bit %u)", value.c_str(), byte_, bit_);
      return;
    }

    size_t index = std::distance(strings_.begin(), it) + 1;  // +1 for 1-based index

    write_bits(data, index);
  }

 private:
  static constexpr const char *TAG = "panasonic_aquarea.extractor.stringarray";
  std::array<std::string, N> strings_;
};

template<size_t KeyLen> class StringMapExtractor : public ExtractorInterface<std::string> {
 public:
  StringMapExtractor(size_t byte, uint8_t bit, uint8_t bit_width,
                     const std::map<std::array<uint8_t, KeyLen>, std::string> &strings)
      : byte_(byte), bit_(bit), bit_width_(bit_width), strings_(strings) {}

  optional<std::string> decode(std::span<const uint8_t> data) override {
    std::array<uint8_t, KeyLen> key;
    std::ranges::copy(data.subspan(byte_, KeyLen), key.begin());

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

    // Shift all bytes to align to the last bit
    if (data_end_bit % 8 != 0) {
      const uint8_t shift = 8 - (data_end_bit % 8);
      for (size_t i = KeyLen - 1; i > 0; --i) {
        key[i] = (key[i] >> shift) | (key[i - 1] << (8 - shift));
      }
      key[0] >>= shift;
    }

    auto it = strings_.find(key);
    if (it == strings_.end()) {
      ESP_LOGW(TAG, "key not found for string extractor (byte %zu, %zu bytes): %s", byte_, KeyLen,
               format_hex_pretty(key.data(), KeyLen).c_str());
      return {"undefined"};
    }

    return it->second;
  }

  void encode(std::span<uint8_t> data, const std::string &value) override {
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

    // Shift all bytes to align to the last bit
    std::array<uint8_t, KeyLen> aligned_key = key;
    if (data_end_bit % 8 != 0) {
      const uint8_t shift = 8 - (data_end_bit % 8);
      for (size_t i = 0; i < KeyLen - 1; ++i) {
        aligned_key[i] = (aligned_key[i] << shift) | (aligned_key[i + 1] >> (8 - shift));
      }
      aligned_key[KeyLen - 1] <<= shift;
    }

    // Field mask per byte
    std::array<uint8_t, KeyLen> field_mask;
    field_mask.fill(0xFF);

    // Mask left bits in first byte
    field_mask[0] &= 0xFF >> (bit_ - 1);

    // Mask right bits in last byte
    field_mask[KeyLen - 1] &= 0xFF << (total_bits - data_end_bit);

    // Merge the aligned key into data, keeping bits outside the field (other entities may share these bytes)
    for (size_t i = 0; i < KeyLen; ++i) {
      data[byte_ + i] = (data[byte_ + i] & ~field_mask[i]) | (aligned_key[i] & field_mask[i]);
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
  using Decoder = std::function<optional<T>(std::span<const uint8_t>)>;
  using Encoder = std::function<void(std::span<uint8_t>, const T &)>;

  LambdaExtractor(Decoder decoder, Encoder encoder = nullptr) : decoder_(decoder), encoder_(encoder) {}
  optional<T> decode(std::span<const uint8_t> data) override { return decoder_(data); }
  void encode(std::span<uint8_t> data, const T &value) override {
    if (encoder_)
      encoder_(data, value);
  }

 private:
  Decoder decoder_;
  Encoder encoder_;
};

}  // namespace panasonic_aquarea
}  // namespace esphome
