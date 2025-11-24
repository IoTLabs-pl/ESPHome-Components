#include "transceiver_cc1101.h"

#include "esphome/core/log.h"
namespace esphome
{
    namespace wmbus_radio
    {
        namespace
        {
            constexpr const char *TAG = "wmbus.cc1101";

            bool gdo_pin_toggles(InternalGPIOPin *pin, const char *name)
            {
                constexpr int gdo_toggle_sample_count = 200;
                constexpr int gdo_sample_delay_us = 15;
                constexpr int min_gdo_transitions = 2;

                if (pin == nullptr)
                {
                    return true;
                }

                pin->setup();
                pin->pin_mode(gpio::FLAG_INPUT);

                bool previous = pin->digital_read();
                int transitions = 0;

                for (int i = 0; i < gdo_toggle_sample_count; ++i)
                {
                    delayMicroseconds(gdo_sample_delay_us);
                    const bool current = pin->digital_read();
                    if (current != previous)
                    {
                        ++transitions;
                        previous = current;
                    }
                }

                if (transitions < min_gdo_transitions)
                {
                    ESP_LOGW(TAG, "%s pin not toggling as expected; wiring may be wrong", name);
                    return false;
                }

                return true;
            }
        } // namespace

        enum class CC1101::Register : uint8_t
        {
            IOCFG2 = 0x00,
            IOCFG1 = 0x01,
            IOCFG0 = 0x02,
        };

        enum class CC1101::Status : uint8_t
        {
            PARTNUM = 0x30,
            VERSION = 0x31,
            RSSI = 0x34,
        };

        enum class CC1101::Strobe : uint8_t
        {
            SRES = 0x30,
            SRX = 0x34,
            SIDLE = 0x36,
        };

        CC1101::CC1101()
            : gdo0_pin_(nullptr), frequency_mhz_(868.95f) {}

        void CC1101::set_gdo0_pin(InternalGPIOPin *pin)
        {
            this->gdo0_pin_ = pin;
        }

        void CC1101::set_frequency(float freq_mhz)
        {
            this->frequency_mhz_ = freq_mhz;
        }

        const char *CC1101::get_name() { return "CC1101"; }

        void CC1101::dump_config()
        {
            ESP_LOGCONFIG(TAG, "Transceiver: %s", this->get_name());
            LOG_PIN("GDO0 Pin: ", this->gdo0_pin_);
            ESP_LOGCONFIG(TAG, "Frequency set: %.2f", this->frequency_mhz_);
        }

        void CC1101::setup()
        {
            constexpr uint8_t version_unset = 0x00;
            constexpr uint8_t version_not_detected = 0xFF;

            ESP_LOGCONFIG(TAG, "Setting up CC1101 transceiver");
            if (this->gdo0_pin_ == nullptr)
            {
                ESP_LOGE(TAG, "CC1101 requires GDO0 to be wired as an IRQ pin.");
                this->mark_failed();
                return;
            }

            this->spi_setup();

            this->reset_();

            const auto partnum = this->read_status_(Status::PARTNUM);
            const auto version = this->read_status_(Status::VERSION);
            if (version == version_unset || version == version_not_detected)
            {
                ESP_LOGE(TAG, "CC1101 not detected (PARTNUM=0x%02X VERSION=0x%02X)",
                         partnum, version);
                this->mark_failed();
                return;
            }

            ESP_LOGD(TAG, "Detected CC1101 PARTNUM=0x%02X VERSION=0x%02X", partnum, version);

            if (!this->verify_gdo_wiring_())
            {
                this->mark_failed();
                return;
            }

            this->restart_rx();
            ESP_LOGCONFIG(TAG, "CC1101 ready (frequency %.2f MHz)", this->frequency_mhz_);
        }

        void CC1101::restart_rx()
        {
            this->send_strobe_(Strobe::SIDLE);
            this->send_strobe_(Strobe::SRX);
        }

        void CC1101::attach_interrupt_impl(void (*callback)(void *), void *arg)
        {
            (void)callback;
            (void)arg;
        }

        size_t CC1101::read(uint8_t *buffer, size_t length)
        {
            return 0;
        }

        int8_t CC1101::get_rssi()
        {
            constexpr int rssi_sign_bit_threshold = 128;
            constexpr int rssi_wraparound = 256;
            constexpr int rssi_divisor = 2;
            constexpr int rssi_offset_dbm = 74;

            const uint8_t rssi_raw = this->read_status_(Status::RSSI);
            int16_t rssi_dbm;
            if (rssi_raw >= rssi_sign_bit_threshold)
            {
                rssi_dbm = ((rssi_raw - rssi_wraparound) / rssi_divisor) - rssi_offset_dbm;
            }
            else
            {
                rssi_dbm = (rssi_raw / rssi_divisor) - rssi_offset_dbm;
            }
            return static_cast<int8_t>(rssi_dbm);
        }

        void CC1101::send_strobe_(Strobe strobe)
        {
            this->delegate_->begin_transaction();
            this->delegate_->transfer(static_cast<uint8_t>(strobe));
            this->delegate_->end_transaction();
        }

        void CC1101::write_register_(Register reg, uint8_t value)
        {
            this->delegate_->begin_transaction();
            this->delegate_->transfer(static_cast<uint8_t>(reg));
            this->delegate_->transfer(value);
            this->delegate_->end_transaction();
        }

        uint8_t CC1101::read_status_(Status status)
        {
            constexpr uint8_t cc1101_read_burst = 0xC0;
            constexpr uint8_t dummy_status_byte = 0x00;
            return this->spi_transaction(cc1101_read_burst, static_cast<uint8_t>(status), {dummy_status_byte});
        }

        bool CC1101::verify_gdo_wiring_()
        {
            constexpr uint8_t gdo_cfg_clk_xosc_div_192 = 0x3F;
            constexpr uint8_t gdo_cfg_hw_to_0 = 0x2F;

            ESP_LOGD(TAG, "Verifying GDO0 wiring using CLK_XOSC/192 output");
            this->write_register_(Register::IOCFG0, gdo_cfg_clk_xosc_div_192);
            const bool gdo0_ok = gdo_pin_toggles(this->gdo0_pin_, "GDO0");
            this->write_register_(Register::IOCFG0, gdo_cfg_hw_to_0);

            if (!gdo0_ok)
            {
                ESP_LOGE(TAG, "GDO0 wiring check failed");
                return false;
            }
            ESP_LOGD(TAG, "GDO0 wiring verification succeeded");
            return gdo0_ok;
        }

        void CC1101::reset_()
        {
            constexpr int reset_settle_delay_ms = 10;

            this->send_strobe_(Strobe::SRES);
            delay(reset_settle_delay_ms);
        }

    } // namespace wmbus_radio
} // namespace esphome
