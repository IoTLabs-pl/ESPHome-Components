#pragma once
#include "esphome/core/component.h"
#include "esphome/components/sensor/sensor.h"
#include "../extractor.h"

namespace esphome {
namespace panasonic_aquarea {
class Sensor : public sensor::Sensor, public Component, public panasonic_aquarea::ReadOnlyEntity<Sensor, float> {
 public:
  void dump_config() override {
    const char *TAG = "panasonic_aquarea.sensor";
    LOG_SENSOR("", "panasonic_aquarea", this);
  }
};

}  // namespace panasonic_aquarea
}  // namespace esphome
