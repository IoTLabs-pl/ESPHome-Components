#include "transceiver_sx.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_radio {
static const char *const TAG = "wmbus.sx_radio";

void SXTransceiver::set_reset_pin(InternalGPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }

void SXTransceiver::set_irq_pin(InternalGPIOPin *irq_pin) { this->irq_pin_ = irq_pin; }

void SXTransceiver::reset() {
  this->reset_pin_->digital_write(0);
  delay(5);
  this->reset_pin_->digital_write(1);
  delay(5);
}

void SXTransceiver::common_setup() {
  this->reset_pin_->setup();
  this->irq_pin_->setup();
  this->spi_setup();
}

void SXTransceiver::attach_interrupt_impl(void (*callback)(void *), void *arg) {
  struct InterruptContext {
    void (*callback)(void *){nullptr};
    void *arg{nullptr};
  };

  static InterruptContext interrupt_context_{};

  interrupt_context_.callback = callback;
  interrupt_context_.arg = arg;
  this->irq_pin_->attach_interrupt<InterruptContext>(
      +[](InterruptContext *ctx) {
        if (ctx != nullptr && ctx->callback != nullptr) {
          ctx->callback(ctx->arg);
        }
      },
      &interrupt_context_, gpio::INTERRUPT_FALLING_EDGE);
}

void SXTransceiver::dump_config() {
  ESP_LOGCONFIG(TAG, "Transceiver: %s", this->get_name());
  LOG_PIN("Reset Pin: ", this->reset_pin_);
  LOG_PIN("IRQ Pin: ", this->irq_pin_);
}
}  // namespace wmbus_radio
}  // namespace esphome
