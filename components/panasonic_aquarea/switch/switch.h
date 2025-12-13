#pragma once
#include "esphome/core/component.h"
#include "esphome/components/switch/switch.h"
#include "../extractor.h"

namespace esphome
{
  namespace panasonic_aquarea
  {
    class Switch : public switch_::Switch,
                   public Component,
                   public panasonic_aquarea::ReadWriteEntity<Switch, bool>
    {
    public:
      void write_state(bool state) override
      {
        this->send_command(state);
      }
      void dump_config() override
      {
        const char *TAG = "panasonic_aquarea.switch";
        LOG_SWITCH("", "panasonic_aquarea", this);
      }
    };
  } // namespace panasonic_aquarea
} // namespace esphome
