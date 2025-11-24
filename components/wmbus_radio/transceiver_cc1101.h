#pragma once

#include "transceiver.h"
#include <cstdint>

namespace esphome
{
    namespace wmbus_radio
    {
        class CC1101 : public RadioTransceiver
        {
        public:
            CC1101();

            void setup() override;
            void restart_rx() override;
            int8_t get_rssi() override;
            const char *get_name() override;
            void dump_config() override;

            void set_gdo0_pin(InternalGPIOPin *pin);
            void set_frequency(float freq_mhz);

        protected:
            void attach_interrupt_impl(void (*callback)(void *), void *arg) override;
            size_t read(uint8_t *buffer, size_t length) override;

        private:
            enum class Register : uint8_t;
            enum class Status : uint8_t;
            enum class Strobe : uint8_t;

            bool verify_gdo_wiring_();
            void reset_();
            void send_strobe_(Strobe strobe);
            void write_register_(Register reg, uint8_t value);
            uint8_t read_status_(Status status);

            InternalGPIOPin *gdo0_pin_;
            float frequency_mhz_;
        };

    } // namespace wmbus_radio
} // namespace esphome
