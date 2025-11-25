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
            void configure_gdo_signals_();
            uint8_t stable_rxbytes_();
            void read_rx_fifo_(uint8_t *buffer, size_t length);
            void rearm_rx_(bool configure);
            void apply_wmbus_rf_config_();
            void recover_from_overflow_();
            bool is_sync_active_();
            void mark_frame_start_if_needed_(bool sync_active);
            bool should_return_without_frame_(bool sync_active) const;
            bool handle_no_rxbytes_(bool sync_active, size_t rxbytes);
            bool handle_overflow_(uint8_t rxbytes);
            size_t guarded_rxbytes_(bool sync_active, size_t rxbytes) const;

            InternalGPIOPin *gdo0_pin_;
            float frequency_mhz_;
            bool frame_active_{false};
        };

    } // namespace wmbus_radio
} // namespace esphome
