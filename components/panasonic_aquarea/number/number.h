#pragma once
#include "esphome/core/component.h"
#include "esphome/components/number/number.h"
#include "../extractor.h"

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

}  // namespace panasonic_aquarea
}  // namespace esphome
