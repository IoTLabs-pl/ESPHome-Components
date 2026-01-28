#pragma once

#include "ring_buffer.h"
#include <forward_list>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/components/uart/uart.h"

#include "protocol.h"
#include "extractor.h"

namespace esphome {
namespace panasonic_aquarea {

// Forward declaration
class Device;

// ==== Entity templates ====

template<typename Derived, typename T> class ReadOnlyEntity : public ReadableEntity {
 public:
  void set_extractor(ExtractorInterface<T> *extractor) { extractor_ = extractor; }

  void handle_update(const std::vector<uint8_t> &data) override {
    auto value = extractor_->decode(data);

    if (!value.has_value()) {
      auto name = static_cast<Derived *>(this)->get_name();
      ESP_LOGV("ReadableEntity", "Value unchanged for %s, not publishing", name.c_str());

      return;
    }
    static_cast<Derived *>(this)->publish_state(*value);
  }

 protected:
  ExtractorInterface<T> *extractor_;
};

template<typename Derived, typename T> class WriteOnlyEntity : public WritableEntity, public Parented<Device> {
 public:
  void set_extractor(ExtractorInterface<T> *extractor) { extractor_ = extractor; }

  void send_command(const T &value);  // Definition after Device class

 protected:
  ExtractorInterface<T> *extractor_;
};

template<typename Derived, typename T>
class ReadWriteEntity : public ReadOnlyEntity<Derived, T>, public WriteOnlyEntity<Derived, T> {
 public:
  void set_extractor(ExtractorInterface<T> *extractor) {
    ReadOnlyEntity<Derived, T>::set_extractor(extractor);
    WriteOnlyEntity<Derived, T>::set_extractor(extractor);
  }
};

class Device : public PollingComponent, public uart::UARTDevice {
 public:
  class SupportsExtraQueryEntity : public ReadOnlyEntity<SupportsExtraQueryEntity, bool> {
   private:
    static constexpr size_t EXTRA_SUPPORT_BYTE_INDEX = 199;
    static constexpr uint8_t EXTRA_SUPPORT_THRESHOLD = 0x03;

   public:
    SupportsExtraQueryEntity();

    bool state{false};
    void publish_state(bool value);
    const std::string get_name() const;
  };

 protected:
  uart::UARTComponent *external_controller_{nullptr};

  SupportsExtraQueryEntity supports_extra_query_entity_;
  std::forward_list<ReadableEntity *> standard_response_entities_;
  std::forward_list<ReadableEntity *> extra_response_entities_;

  std::vector<uint8_t> awaiting_command_data = std::vector<uint8_t>(Protocol::STANDARD_PAYLOAD_LENGTH);
  bool awaiting_command_dirty_flag_{false};

  enum class CommunicationState {
    IDLE,
    // AWAITING_RESPONSE,
    INTERNAL_TRANSACTION,
    EXTERNAL_TRANSACTION
  };

  CommunicationState comm_state_;  // Mutex alike (we call all routines from single thread)

  bool start_response_timeout(bool internal);
  void stop_response_timeout();

  ResponseBuffer response_buffer_;

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
  void add_entity(ReadableEntity *entity, bool extra = false);
  std::vector<uint8_t> &get_command_data() {
    this->awaiting_command_dirty_flag_ = true;
    return this->awaiting_command_data;
  }
};

// ==== WriteOnlyEntity::send_command implementation (after Device is fully defined) ====

template<typename Derived, typename T>
void WriteOnlyEntity<Derived, T>::send_command(const T &value) {
  this->extractor_->encode(this->parent_->get_command_data(), value);
}

}  // namespace panasonic_aquarea
}  // namespace esphome
