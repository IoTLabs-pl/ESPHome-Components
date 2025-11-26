#include "transceiver_sx1262.h"

#include "esphome/core/log.h"

namespace esphome
{
    namespace wmbus_radio
    {
        static const char *TAG = "SX1262";

        void SX1262::setup()
        {
            this->common_setup();
            
            ESP_LOGV(TAG, "Setup");
            ESP_LOGVV(TAG, "reset");
            this->reset();
          
            ESP_LOGVV(TAG, "checking silicon revision");
            uint32_t revision = 0x1262; // this->spi_read(0x42);
            ESP_LOGVV(TAG, "revision: %04X", revision);
            if (revision != 0x1262) {
              ESP_LOGE(TAG, "Invalid silicon revision: %04X", revision);
              return;
            }
          
            ESP_LOGVV(TAG, "setting Standby mode");
            this->spi_write(RADIOLIB_SX126X_CMD_SET_STANDBY, {RADIOLIB_SX126X_STANDBY_RC});
          
            ESP_LOGVV(TAG, "setting packet type");
            this->spi_write(RADIOLIB_SX126X_CMD_SET_PACKET_TYPE, {RADIOLIB_SX126X_PACKET_TYPE_GFSK});
          
            ESP_LOGVV(TAG, "setting radio frequency");
            const float frequency = 868.950;
            const uint32_t frf = (frequency * (uint32_t(1) << RADIOLIB_SX126X_DIV_EXPONENT)) / RADIOLIB_SX126X_CRYSTAL_FREQ;
            this->spi_write(RADIOLIB_SX126X_CMD_SET_RF_FREQUENCY, {
                            BYTE(frf, 3), BYTE(frf, 2), BYTE(frf, 1), BYTE(frf, 0)});
          
            ESP_LOGVV(TAG, "setting buffer base adress");
            this->spi_write(RADIOLIB_SX126X_CMD_SET_BUFFER_BASE_ADDRESS, {0x00, 0x00});
          
            ESP_LOGVV(TAG, "setting modulation parameters");
            uint32_t bitrate = (uint32_t)((RADIOLIB_SX126X_CRYSTAL_FREQ * 1000000.0f * 32.0f) / (100.0f * 1000.0f));
            uint32_t freqdev = (uint32_t)(((50.0f * 1000.0f) * (float)((uint32_t)(1) << 25)) / (RADIOLIB_SX126X_CRYSTAL_FREQ * 1000000.0f));
            this->spi_write(RADIOLIB_SX126X_CMD_SET_MODULATION_PARAMS, {
                            BYTE(bitrate, 2), BYTE(bitrate, 1), BYTE(bitrate, 0),
                            RADIOLIB_SX126X_GFSK_FILTER_NONE,
                            RADIOLIB_SX126X_GFSK_RX_BW_234_3,
                            BYTE(freqdev, 2), BYTE(freqdev, 1), BYTE(freqdev, 0)
            });
          
            ESP_LOGVV(TAG, "setting packet parameters");
            this->spi_write(RADIOLIB_SX126X_CMD_SET_PACKET_PARAMS, {
                            BYTE(16, 1), BYTE(16, 0),   // Preamble length
                            RADIOLIB_SX126X_GFSK_PREAMBLE_DETECT_8, 
                            16,                         // Sync word bit length
                            RADIOLIB_SX126X_GFSK_ADDRESS_FILT_OFF, 
                            RADIOLIB_SX126X_GFSK_PACKET_FIXED, 
                            0xff,                       // Payload length
                            RADIOLIB_SX126X_GFSK_CRC_OFF, 
                            RADIOLIB_SX126X_GFSK_WHITENING_OFF
            });
            
            ESP_LOGVV(TAG, "setting RX gain");
            this->spi_write(RADIOLIB_SX126X_CMD_WRITE_REGISTER, {
                            BYTE(RADIOLIB_SX126X_REG_RX_GAIN, 1), BYTE(RADIOLIB_SX126X_REG_RX_GAIN, 0),
                            RADIOLIB_SX126X_RX_GAIN_POWER_SAVING
            });
          
            ESP_LOGVV(TAG, "setting IRQ parameters");
            const uint32_t irqmask = RADIOLIB_SX126X_IRQ_RX_DONE;
            this->spi_write(RADIOLIB_SX126X_CMD_SET_DIO_IRQ_PARAMS, {
                            BYTE(irqmask, 1), BYTE(irqmask, 0),
                            BYTE(irqmask, 1), BYTE(irqmask, 0),
                            0x00, 0x00,
                            0x00, 0x00
            });
          
            ESP_LOGVV(TAG, "setting sync word");
            this->spi_write(RADIOLIB_SX126X_CMD_WRITE_REGISTER, {
                            BYTE(RADIOLIB_SX126X_REG_SYNC_WORD_0, 1), BYTE(RADIOLIB_SX126X_REG_SYNC_WORD_0, 0),
                            0x54, 0x3d, 0x00, 0x00, 0x00, 0x00
            });
          
            ESP_LOGVV(TAG, "setting DIO3 as TCXO control");
            const uint32_t tcxodelay = 64;
            this->spi_write(RADIOLIB_SX126X_CMD_SET_DIO3_AS_TCXO_CTRL, {
                            RADIOLIB_SX126X_DIO3_OUTPUT_3_0,
                            BYTE(tcxodelay, 2), BYTE(tcxodelay, 1), BYTE(tcxodelay, 0)
            });
          
            ESP_LOGVV(TAG, "setting fallback mode");
            this->spi_write(RADIOLIB_SX126X_CMD_SET_RX_TX_FALLBACK_MODE, {RADIOLIB_SX126X_RX_TX_FALLBACK_MODE_STDBY_XOSC});
          
            ESP_LOGVV(TAG, "setting Standby mode");
            this->spi_write(RADIOLIB_SX126X_CMD_SET_STANDBY, {RADIOLIB_SX126X_STANDBY_XOSC});
          
            ESP_LOGVV(TAG, "setting RX mode");
            const uint32_t timeout = 0x000000; // 0xFFFFFF;
            this->spi_write(RADIOLIB_SX126X_CMD_SET_RX, {
                            BYTE(timeout, 2), BYTE(timeout, 1), BYTE(timeout, 0)
            });
          
            ESP_LOGV(TAG, "SX1262 setup done");
        }

        size_t SX1262::read(uint8_t *buffer, size_t length)
        {
            constexpr uint8_t kFifoRegister = 0x00;

            if (length != 0 && this->irq_pin_->digital_read() == false)
            {
                buffer[0] = this->spi_read(kFifoRegister);
                return 1;
            }
            return 0;
        }

        void SX1262::restart_rx()
        {
            ESP_LOGD(TAG, "Restarting RX");
            // Standby mode
            this->spi_write(RADIOLIB_SX126X_CMD_SET_STANDBY, {RADIOLIB_SX126X_STANDBY_XOSC});
            delay(5);
          
            // Clear IRQ
            const uint32_t irqmask = RADIOLIB_SX126X_IRQ_RX_DONE; // | RADIOLIB_SX126X_IRQ_SYNC_WORD_VALID;
            this->spi_write(RADIOLIB_SX126X_CMD_CLEAR_IRQ_STATUS, {
                            BYTE(irqmask, 1), BYTE(irqmask, 0)
            });
          
            // Enable RX
            const uint32_t timeout = 0x000000; // 0xffffff;
            this->spi_write(RADIOLIB_SX126X_CMD_SET_RX, {
                            BYTE(timeout, 2), BYTE(timeout, 1), BYTE(timeout, 0)
            });
            delay(5);
        }

        int8_t SX1262::get_rssi()
        {
            uint8_t rssi = this->spi_read(RADIOLIB_SX126X_CMD_GET_PACKET_STATUS);
            return (int8_t)(-rssi / 2);
        }

        const char *SX1262::get_name()
        {
            return TAG;
        }
    }
}
