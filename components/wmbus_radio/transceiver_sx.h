#pragma once

#include "transceiver.h"

namespace esphome
{
    namespace wmbus_radio
    {
        class SXRadio : public RadioTransceiver
        {
        public:
            void set_reset_pin(InternalGPIOPin *reset_pin);
            void set_irq_pin(InternalGPIOPin *irq_pin);

            void dump_config() override;

        protected:
            void reset();
            void common_setup();
            void attach_interrupt_impl(void (*callback)(void *), void *arg) override;

            InternalGPIOPin *reset_pin_{nullptr};
            InternalGPIOPin *irq_pin_{nullptr};
        };
    } // namespace wmbus_radio
} // namespace esphome
