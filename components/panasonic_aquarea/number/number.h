#pragma once
#include "esphome/core/component.h"
#include "esphome/components/number/number.h"
#include "../panasonic_aquarea.h"

namespace esphome {
namespace panasonic_aquarea {

class Number : public number::Number, public Component, public panasonic_aquarea::ReadWriteEntity<Number, float> {
 public:
  void control(float value) override { this->send_command(value); }
  void dump_config() override {
    const char *TAG = "panasonic_aquarea.number";
    LOG_NUMBER("", "panasonic_aquarea", this);
  }
};

class VariantNumber : public Number, public panasonic_aquarea::VariantMixin<VariantNumber> {
 public:
  void setup() override { this->setup_variants(); }
  void handle_update(std::span<const uint8_t> data) override {
    this->evaluate_variants(data);
    Number::handle_update(data);
  }
};

}  // namespace panasonic_aquarea
}  // namespace esphome
