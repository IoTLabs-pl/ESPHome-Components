#pragma once
#include "esphome/core/component.h"
#include "esphome/components/button/button.h"
#include "../extractor.h"

namespace esphome
{
  namespace panasonic_aquarea
  {
    class Button : public button::Button,
                   public Component,
                   public panasonic_aquarea::WriteOnlyEntity<Button, bool>
    {
    public:
      void press_action() override
      {
        this->send_command(true);
      }
      void dump_config() override
      {
        const char *TAG = "panasonic_aquarea.button";
        LOG_BUTTON("", "panasonic_aquarea", this);
      }
    };
  } // namespace panasonic_aquarea
} // namespace esphome
