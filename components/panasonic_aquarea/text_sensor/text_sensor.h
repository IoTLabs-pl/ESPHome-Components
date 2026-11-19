#pragma once
#include <span>

#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../panasonic_aquarea.h"

namespace esphome {
namespace panasonic_aquarea {
class TextSensor : public text_sensor::TextSensor,
                   public Component,
                   public panasonic_aquarea::ReadOnlyEntity<TextSensor, std::string> {
 public:
  void dump_config() override {
    const char *TAG = "panasonic_aquarea.text_sensor";
    LOG_TEXT_SENSOR("", "panasonic_aquarea", this);
  }
};

class LabelTextSensor : public text_sensor::TextSensor,
                        public Component,
                        public panasonic_aquarea::ReadOnlyEntity<LabelTextSensor, size_t> {
 public:
  using text_sensor::TextSensor::publish_state;
  void publish_state(size_t index) { this->publish_state(this->labels_[index]); }
  void set_labels(std::span<const char *const> labels) { this->labels_ = labels; }
  void dump_config() override {
    const char *TAG = "panasonic_aquarea.text_sensor";
    LOG_TEXT_SENSOR("", "panasonic_aquarea", this);
  }

 protected:
  std::span<const char *const> labels_;
};

}  // namespace panasonic_aquarea
}  // namespace esphome
