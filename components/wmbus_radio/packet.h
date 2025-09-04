#pragma once
#include <cstdint>
#include <cstddef>
#include <ctime>
#include <optional>
#include <string>
#include <vector>

#include "esphome/core/helpers.h"
#include "esphome/components/wmbus_common/wmbus.h"

namespace esphome
{
    namespace wmbus_radio
    {
        struct Frame;

        struct Packet
        {
            friend class Frame;

        public:
            Packet();

            uint8_t *rx_data_ptr();
            size_t rx_capacity();
            bool calculate_payload_size();
            void set_rssi(int8_t rssi);

            std::optional<Frame> convert_to_frame();

        protected:
            std::vector<uint8_t> data_;

            size_t expected_size();
            size_t expected_size_ = 0;

            uint8_t l_field();
            int8_t rssi_ = 0;

            LinkMode link_mode();
            LinkMode link_mode_ = LinkMode::UNKNOWN;

            std::string frame_format_;
        };

        struct Frame
        {
        public:
            Frame(Packet *packet);
            Frame(std::vector<uint8_t> data, LinkMode lm, int8_t rssi) : data_(std::move(data)), link_mode_(lm), rssi_(rssi), format_(format) {};

            std::vector<uint8_t> &data();
            LinkMode link_mode();
            int8_t rssi();
            std::string format();

            std::vector<uint8_t> as_raw();
            std::string as_hex();
            std::string as_rtlwmbus();
            std::string meter_id();

            void mark_as_handled();
            uint8_t handlers_count();

        protected:
            std::vector<uint8_t> data_;
            LinkMode link_mode_;
            int8_t rssi_;
            std::string format_;
            uint8_t handlers_count_ = 0;
        };

    }
} // namespace esphome
