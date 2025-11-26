#pragma once
#include "transceiver_sx.h"

namespace esphome
{
    namespace wmbus_radio
    {
        class SX1276 : public SXRadio
        {
        public:
            void setup() override;
            size_t read(uint8_t *buffer, size_t length) override;
            void restart_rx() override;
            int8_t get_rssi() override;
            const char * get_name() override;
        };
    }
}
