#pragma once

#include <array>
#include <forward_list>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/preferences.h"
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
  ReadOnlyEntity() = default;

  void set_extractor(ExtractorInterface<T> *extractor) { extractor_ = extractor; }

  void handle_update(std::span<const uint8_t> data) override {
    auto value = extractor_->decode(data);

    if (!value.has_value() || publish_dedup_.next(*value) == false) {
      auto name = static_cast<Derived *>(this)->get_name();
      ESP_LOGV("ReadableEntity", "Value unchanged for %s, not publishing", name.c_str());

      return;
    }
    static_cast<Derived *>(this)->publish_state(*value);
  }

 protected:
  ExtractorInterface<T> *extractor_{nullptr};
  Deduplicator<T> publish_dedup_;
};

template<typename Derived, typename T> class WriteOnlyEntity : public WritableEntity, public Parented<Device> {
 public:
  void set_extractor(ExtractorInterface<T> *extractor) { extractor_ = extractor; }

  void send_command(const T &value);  // Definition after Device class

 protected:
  ExtractorInterface<T> *extractor_{nullptr};
};

template<typename Derived, typename T>
class ReadWriteEntity : public ReadOnlyEntity<Derived, T>, public WriteOnlyEntity<Derived, T> {
 public:
  ReadWriteEntity() = default;

  void set_extractor(ExtractorInterface<T> *extractor) {
    ReadOnlyEntity<Derived, T>::set_extractor(extractor);
    WriteOnlyEntity<Derived, T>::set_extractor(extractor);
  }
};

template<typename Derived> class VariantMixin {
 public:
  struct Variant {
    bool (*condition)(std::span<const uint8_t> data);
    void (*apply)(Derived *);
  };

  void set_variants(uint32_t pref_key, std::span<const Variant> variants) {
    this->pref_key_ = pref_key;
    this->variants_ = variants;
  }

  void setup_variants() {
    this->variant_pref_ = global_preferences->make_preference<uint8_t>(this->pref_key_);

    uint8_t saved_index = 0;
    if (this->variant_pref_.load(&saved_index) && saved_index < this->variants_.size()) {
      this->variant_index_ = saved_index;
    }

    auto name = static_cast<Derived *>(this)->get_name();
    ESP_LOGI("panasonic_aquarea", "%s loaded variant %u from preferences", name.c_str(), this->variant_index_);

    this->variants_[this->variant_index_].apply(static_cast<Derived *>(this));
  }

  void evaluate_variants(std::span<const uint8_t> data) {
    uint8_t matched_variant = 0;
    for (size_t i = 0; i < this->variants_.size(); i++) {
      if (this->variants_[i].condition(data)) {
        matched_variant = static_cast<uint8_t>(i);
        break;
      }
    }

    if (this->variant_index_ != matched_variant) {
      this->variant_index_ = matched_variant;
      this->variant_pref_.save(&this->variant_index_);
      auto name = static_cast<Derived *>(this)->get_name();
      ESP_LOGI("panasonic_aquarea", "%s detected variant %u, saved to preferences (applied after restart)",
               name.c_str(), matched_variant);
    }
  }

 protected:
  uint32_t pref_key_{0};
  uint8_t variant_index_{0};
  std::span<const Variant> variants_{};
  ESPPreferenceObject variant_pref_;
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

  Protocol::RequestFrame awaiting_command_data{};
  bool awaiting_command_dirty_flag_{false};

  enum class CommunicationState {
    IDLE,
    // AWAITING_RESPONSE,
    INTERNAL_TRANSACTION,
    EXTERNAL_TRANSACTION
  };

  CommunicationState comm_state_{CommunicationState::IDLE};  // Mutex alike (we call all routines from single thread)

  bool start_response_timeout(bool internal);
  void stop_response_timeout();

  Protocol::Parser response_parser_;

  uint32_t request_counter_{0};

  void yield_to_external_controller();

  // Command queue processing
  void handle_command_queue();

  // UART data processing
  void process_heatpump_data();
  void process_external_controller_data();

  // Response handling
  void handle_response();

 public:
  void set_external_controller_uart(uart::UARTComponent *controller);
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override;
  void add_entity(ReadableEntity *entity, bool extra = false);
  Protocol::RequestFrame &get_command_data() {
    this->awaiting_command_dirty_flag_ = true;
    return this->awaiting_command_data;
  }
};

// ==== WriteOnlyEntity::send_command implementation (after Device is fully defined) ====

template<typename Derived, typename T> void WriteOnlyEntity<Derived, T>::send_command(const T &value) {
  this->extractor_->encode(this->parent_->get_command_data(), value);
}

}  // namespace panasonic_aquarea
}  // namespace esphome
