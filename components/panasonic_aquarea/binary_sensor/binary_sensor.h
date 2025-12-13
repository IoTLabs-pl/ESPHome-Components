#pragma once
#include "esphome/core/component.h"
#include "esphome/components/binary_sensor/binary_sensor.h"
#include "../extractor.h"

namespace esphome
{
  namespace panasonic_aquarea
  {
    class BinarySensor : public binary_sensor::BinarySensor,
                         public Component,
                         public panasonic_aquarea::ReadOnlyEntity<BinarySensor, bool>
    {
    public:
      void dump_config() override
      {
        const char *TAG = "panasonic_aquarea.binary_sensor";
        LOG_BINARY_SENSOR("", "panasonic_aquarea", this);
      }
    };
  } // namespace panasonic_aquarea
} // namespace esphome
