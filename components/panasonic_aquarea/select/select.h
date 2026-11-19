#pragma once
#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "../panasonic_aquarea.h"

namespace esphome {
namespace panasonic_aquarea {
class Select : public select::Select, public Component, public panasonic_aquarea::ReadWriteEntity<Select, size_t> {
 public:
  void control(size_t index) override { this->send_command(index); }
  void dump_config() override {
    const char *TAG = "panasonic_aquarea.select";
    LOG_SELECT("", "Panasonic Heatpump Select", this);
  }
};

}  // namespace panasonic_aquarea
}  // namespace esphome
