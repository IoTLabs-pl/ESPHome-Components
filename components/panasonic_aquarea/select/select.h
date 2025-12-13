#pragma once
#include "esphome/core/component.h"
#include "esphome/components/select/select.h"
#include "../extractor.h"

namespace esphome {
namespace panasonic_aquarea {
class Select : public select::Select, public Component, public panasonic_aquarea::ReadWriteEntity<Select, std::string> {
 public:
  void control(const std::string &value) override { this->send_command(value); }
  void dump_config() override {
    const char *TAG = "panasonic_aquarea.select";
    LOG_SELECT("", "Panasonic Heatpump Select", this);
  }
};

}  // namespace panasonic_aquarea
}  // namespace esphome
