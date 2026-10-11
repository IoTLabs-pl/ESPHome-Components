#pragma once
#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "../panasonic_aquarea.h"

namespace esphome {
namespace panasonic_aquarea {
class Button : public button::Button, public Component, public panasonic_aquarea::WriteOnlyEntity<Button, bool> {
 public:
  void set_press_value(bool value) { this->press_value_ = value; }
  void press_action() override { this->send_command(this->press_value_); }
  void dump_config() override {
    const char *TAG = "panasonic_aquarea.button";
    LOG_BUTTON("", "panasonic_aquarea", this);
  }

 protected:
  bool press_value_{true};
};
}  // namespace panasonic_aquarea
}  // namespace esphome
