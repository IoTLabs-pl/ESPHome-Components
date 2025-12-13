#pragma once
#include "esphome/core/component.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "../extractor.h"

namespace esphome
{
  namespace panasonic_aquarea
  {
    class TextSensor : public text_sensor::TextSensor,
                       public Component,
                       public panasonic_aquarea::ReadOnlyEntity<TextSensor, std::string>
    {
    public:
      void dump_config() override
      {
        const char *TAG = "panasonic_aquarea.text_sensor";
        LOG_TEXT_SENSOR("", "panasonic_aquarea", this);
      }
    };

  } // namespace panasonic_aquarea
} // namespace esphome
