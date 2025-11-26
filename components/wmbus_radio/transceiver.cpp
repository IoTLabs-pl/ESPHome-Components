#include "transceiver.h"

#include "esphome/core/log.h"

#include "freertos/FreeRTOS.h"

namespace esphome
{
    namespace wmbus_radio
    {
        static const char *TAG = "wmbus.transceiver";

        bool RadioTransceiver::read_in_task(uint8_t *buffer, size_t length)
        {
            uint8_t *buffer_end = buffer + length;
            constexpr TickType_t kReadWaitMs = 5;

            while (buffer != buffer_end)
            {
                const size_t remaining = static_cast<size_t>(buffer_end - buffer);
                size_t bytes_read = this->read(buffer, remaining);
                if (bytes_read > remaining)
                {
                    bytes_read = remaining;
                }

                if (bytes_read > 0)
                {
                    buffer += bytes_read;
                    continue;
                }

                if (!ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(kReadWaitMs)))
                {
                    return false;
                }
            }

            return true;
        }

        uint8_t RadioTransceiver::spi_transaction(uint8_t operation, uint8_t address, std::initializer_list<uint8_t> data)
        {
            this->delegate_->begin_transaction();
            auto rval = this->delegate_->transfer(operation | address);
            for (auto byte : data)
                rval = this->delegate_->transfer(byte);
            this->delegate_->end_transaction();
            return rval;
        }

        uint8_t RadioTransceiver::spi_read(uint8_t address)
        {
            return this->spi_transaction(0x00, address, {0});
        }

        void RadioTransceiver::spi_write(uint8_t address, std::initializer_list<uint8_t> data)
        {
            this->spi_transaction(0x80, address, data);
        }

        void RadioTransceiver::spi_write(uint8_t address, uint8_t data)
        {
            this->spi_write(address, {data});
        }

        void RadioTransceiver::dump_config()
        {
            ESP_LOGCONFIG(TAG, "Transceiver: %s", this->get_name());
        }
    }
}
