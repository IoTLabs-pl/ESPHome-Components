#include "transceiver_cc1101.h"

#include "esphome/core/log.h"
#include <algorithm>
namespace esphome
{
    namespace wmbus_radio
    {
        namespace
        {
            constexpr const char *TAG = "wmbus.cc1101";
            constexpr uint8_t GDO_CFG_SYNC_DETECT = 0x06;
            constexpr uint8_t RXBYTES_OVERFLOW_MASK = 0x80;

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
            FIFOTHR = 0x03,
            SYNC1 = 0x04,
            SYNC0 = 0x05,
            PKTLEN = 0x06,
            PKTCTRL1 = 0x07,
            PKTCTRL0 = 0x08,
            ADDR = 0x09,
            CHANNR = 0x0A,
            FSCTRL1 = 0x0B,
            FSCTRL0 = 0x0C,
            FREQ2 = 0x0D,
            FREQ1 = 0x0E,
            FREQ0 = 0x0F,
            MDMCFG4 = 0x10,
            MDMCFG3 = 0x11,
            MDMCFG2 = 0x12,
            MDMCFG1 = 0x13,
            MDMCFG0 = 0x14,
            DEVIATN = 0x15,
            MCSM2 = 0x17,
            MCSM0 = 0x18,
            FOCCFG = 0x19,
            BSCFG = 0x1A,
            AGCCTRL2 = 0x1B,
            AGCCTRL1 = 0x1C,
            AGCCTRL0 = 0x1D,
            WOREVT1 = 0x1E,
            WOREVT0 = 0x1F,
            WORCTRL = 0x20,
            FREND1 = 0x21,
            FREND0 = 0x22,
            FSCAL3 = 0x23,
            FSCAL2 = 0x24,
            FSCAL1 = 0x25,
            FSCAL0 = 0x26,
            RCCTRL1 = 0x27,
            RCCTRL0 = 0x28,
            TEST2 = 0x2C,
            TEST1 = 0x2D,
            TEST0 = 0x2E,
            FSTEST = 0x29,
            PTEST = 0x2A,
            AGCTEST = 0x2B,
        };

        enum class CC1101::Status : uint8_t
        {
            PARTNUM = 0x30,
            VERSION = 0x31,
            RSSI = 0x34,
            MARCSTATE = 0x35,
            RXBYTES = 0x3B,
        };

        enum class CC1101::Strobe : uint8_t
        {
            SRES = 0x30,
            SRX = 0x34,
            SFRX = 0x3A,
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

            this->gdo0_pin_->setup();
            this->gdo0_pin_->pin_mode(gpio::FLAG_INPUT);
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

            this->apply_wmbus_rf_config_();
            this->configure_gdo_signals_();
            this->restart_rx();
            ESP_LOGCONFIG(TAG, "CC1101 ready (frequency %.2f MHz)", this->frequency_mhz_);
        }

        void CC1101::restart_rx()
        {
            this->frame_active_ = false;
            this->rearm_rx_(true);
        }

        void CC1101::attach_interrupt_impl(void (*callback)(void *), void *arg)
        {
            if (this->gdo0_pin_ == nullptr)
            {
                return;
            }

            struct InterruptContext
            {
                void (*callback)(void *){nullptr};
                void *arg{nullptr};
            };

            static InterruptContext interrupt_context_{};
            interrupt_context_.callback = callback;
            interrupt_context_.arg = arg;

            this->gdo0_pin_->attach_interrupt<InterruptContext>(
                [](InterruptContext *ctx)
                {
                    if (ctx != nullptr && ctx->callback != nullptr)
                    {
                        ctx->callback(ctx->arg);
                    }
                },
                &interrupt_context_,
                gpio::INTERRUPT_RISING_EDGE);
        }

        size_t CC1101::read(uint8_t *buffer, size_t length)
        {
            constexpr size_t kGuardBytes = 1;
            if (length == 0)
            {
                return 0;
            }

            const bool sync_active = this->is_sync_active_();
            this->mark_frame_start_if_needed_(sync_active);
            if (this->should_return_without_frame_(sync_active))
            {
                return 0;
            }

            auto rxbytes = this->stable_rxbytes_();
            if (this->handle_overflow_(rxbytes))
            {
                return 0;
            }

            rxbytes = this->guarded_rxbytes_(sync_active, static_cast<size_t>(rxbytes));
            if (this->handle_no_rxbytes_(sync_active, rxbytes))
            {
                return 0;
            }

            size_t to_read = rxbytes;
            if (sync_active && to_read > 0)
            {
                if (to_read == kGuardBytes)
                {
                    return 0;
                }
                to_read -= kGuardBytes;
            }
            const size_t bytes_to_copy = std::min(to_read, length);
            this->read_rx_fifo_(buffer, bytes_to_copy);
            return bytes_to_copy;
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

        void CC1101::configure_gdo_signals_()
        {
            this->write_register_(Register::FIFOTHR, 0x07);
            this->write_register_(Register::IOCFG0, GDO_CFG_SYNC_DETECT);
        }

        uint8_t CC1101::stable_rxbytes_()
        {
            const auto first = this->read_status_(Status::RXBYTES);
            const auto second = this->read_status_(Status::RXBYTES);
            if (first == second)
            {
                return first;
            }
            return this->read_status_(Status::RXBYTES);
        }

        void CC1101::apply_wmbus_rf_config_()
        {
            static const std::pair<Register, uint8_t> base_settings[] = {
                {Register::IOCFG0, GDO_CFG_SYNC_DETECT},
                {Register::IOCFG1, 0x2E},
                {Register::FIFOTHR, 0x07},
                {Register::PKTLEN, 0xFF},
                {Register::PKTCTRL1, 0x00},
                {Register::PKTCTRL0, 0x02},
                {Register::ADDR, 0x00},
                {Register::CHANNR, 0x00},
                {Register::FSCTRL1, 0x08},
                {Register::FSCTRL0, 0x00},
                {Register::MDMCFG4, 0x5C},
                {Register::MDMCFG3, 0x04},
                {Register::MDMCFG2, 0x06},
                {Register::MDMCFG1, 0x22},
                {Register::MDMCFG0, 0xF8},
                {Register::DEVIATN, 0x44},
                {Register::MCSM2, 0x07},
                {Register::MCSM0, 0x18},
                {Register::FOCCFG, 0x2E},
                {Register::BSCFG, 0xBF},
                {Register::AGCCTRL2, 0x43},
                {Register::AGCCTRL1, 0x09},
                {Register::AGCCTRL0, 0xB5},
                {Register::WOREVT1, 0x87},
                {Register::WOREVT0, 0x6B},
                {Register::WORCTRL, 0xFB},
                {Register::FREND1, 0xB6},
                {Register::FREND0, 0x10},
                {Register::FSCAL3, 0xEA},
                {Register::FSCAL2, 0x2A},
                {Register::FSCAL1, 0x00},
                {Register::FSCAL0, 0x1F},
                {Register::RCCTRL1, 0x41},
                {Register::RCCTRL0, 0x00},
                {Register::FSTEST, 0x59},
                {Register::PTEST, 0x7F},
                {Register::AGCTEST, 0x3F},
                {Register::TEST2, 0x81},
                {Register::TEST1, 0x35},
                {Register::TEST0, 0x09},
            };

            for (const auto &setting : base_settings)
            {
                this->write_register_(setting.first, setting.second);
            }

            this->write_register_(Register::SYNC1, 0x54);
            this->write_register_(Register::SYNC0, 0x3D);

            const float freq = this->frequency_mhz_;
            const uint32_t freq_word = static_cast<uint32_t>((freq * 1'000'000.0f / 26'000'000.0f) * (1ULL << 16));
            this->write_register_(Register::FREQ2, static_cast<uint8_t>((freq_word >> 16) & 0xFF));
            this->write_register_(Register::FREQ1, static_cast<uint8_t>((freq_word >> 8) & 0xFF));
            this->write_register_(Register::FREQ0, static_cast<uint8_t>(freq_word & 0xFF));
        }

        void CC1101::read_rx_fifo_(uint8_t *buffer, size_t length)
        {
            constexpr uint8_t rx_fifo_burst_address = 0x3F;
            if (length == 0)
            {
                return;
            }

            this->delegate_->begin_transaction();
            this->delegate_->transfer(static_cast<uint8_t>(rx_fifo_burst_address | 0xC0));
            for (size_t i = 0; i < length; ++i)
            {
                buffer[i] = this->delegate_->transfer(0x00);
            }
            this->delegate_->end_transaction();
        }

        void CC1101::rearm_rx_(bool configure)
        {
            constexpr uint8_t pktctrl0_infinite_packet = 0x02;
            constexpr uint8_t fifothr_four_bytes = 0x07;
            this->send_strobe_(Strobe::SIDLE);
            this->send_strobe_(Strobe::SFRX);
            if (configure)
            {
                this->configure_gdo_signals_();
                this->write_register_(Register::PKTCTRL0, pktctrl0_infinite_packet);
                this->write_register_(Register::FIFOTHR, fifothr_four_bytes);
            }
            this->send_strobe_(Strobe::SRX);
        }

        void CC1101::recover_from_overflow_()
        {
            this->frame_active_ = false;
            this->rearm_rx_(false);
        }

        bool CC1101::is_sync_active_()
        {
            return this->gdo0_pin_->digital_read();
        }

        void CC1101::mark_frame_start_if_needed_(bool sync_active)
        {
            if (sync_active && !this->frame_active_)
            {
                this->frame_active_ = true;
            }
        }

        bool CC1101::should_return_without_frame_(bool sync_active) const
        {
            if (!sync_active && !this->frame_active_)
            {
                return true;
            }
            return false;
        }

        bool CC1101::handle_no_rxbytes_(bool sync_active, size_t rxbytes)
        {
            if (rxbytes == 0)
            {
                if (!sync_active && this->frame_active_)
                {
                    this->frame_active_ = false;
                    this->rearm_rx_(false);
                }
                return true;
            }
            return false;
        }

        bool CC1101::handle_overflow_(uint8_t rxbytes)
        {
            if ((rxbytes & RXBYTES_OVERFLOW_MASK) != 0)
            {
                this->recover_from_overflow_();
                return true;
            }
            return false;
        }

        size_t CC1101::guarded_rxbytes_(bool sync_active, size_t rxbytes) const
        {
            rxbytes &= ~RXBYTES_OVERFLOW_MASK;
            return rxbytes;
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
