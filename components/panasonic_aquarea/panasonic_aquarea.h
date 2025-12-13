#pragma once

#include "ring_buffer.h"
#include <forward_list>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"

#include "protocol.h"

namespace esphome
{
  namespace panasonic_aquarea
  {
    class Device;

    class ReadableEntity
    {
    public:
      virtual ~ReadableEntity() = default;
      virtual void handle_update(const std::vector<uint8_t> &data) = 0;
    };

    class WritableEntity : public Parented<Device>
    {
    };

    class Device : public PollingComponent, public uart::UARTDevice
    {
    protected:
      uart::UARTComponent *external_controller_{nullptr};
      std::forward_list<ReadableEntity *> standard_response_entities_;
      std::forward_list<ReadableEntity *> extra_response_entities_;

      std::vector<uint8_t> awaiting_command_data = std::vector<uint8_t>(Protocol::STANDARD_PAYLOAD_LENGTH);
      bool awaiting_command_dirty_flag_{false};

      enum class CommunicationState
      {
        IDLE,
        // AWAITING_RESPONSE,
        INTERNAL_TRANSACTION,
        EXTERNAL_TRANSACTION
      };

      CommunicationState comm_state_; // Mutex alike (we call all routines from single thread)

      bool start_response_timeout(bool internal);
      void stop_response_timeout();

      ResponseBuffer response_buffer_;
      bool supports_extra_query_{false};

      uint32_t request_counter_{0};

      // Command queue processing
      void handle_command_queue();

      // UART data processing
      void process_heatpump_data();
      void process_external_controller_data();

      // Protocol parsing
      bool parse_out_response();

    public:
      void set_external_controller_uart(uart::UARTComponent *controller);
      void setup() override;
      void loop() override;
      void update() override;
      void dump_config() override;
      float get_setup_priority() const override;
      void add_entity(ReadableEntity *entity, Protocol::CategoryByte type = Protocol::CategoryByte::STANDARD);
      std::vector<uint8_t> &get_command_data()
      {
        this->awaiting_command_dirty_flag_ = true;
        return this->awaiting_command_data;
      }
    };

  } // namespace panasonic_aquarea
} // namespace esphome
