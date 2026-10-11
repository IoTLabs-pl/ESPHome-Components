#include "transceiver.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_radio {

static const char *TAG = "wmbus.transceiver";

void Transceiver::set_reset_pin(InternalGPIOPin *reset_pin) { this->reset_pin_ = reset_pin; }

void Transceiver::set_dio1_pin(InternalGPIOPin *dio1_pin) { this->dio1_pin_ = dio1_pin; }

void Transceiver::reset() {
  this->reset_pin_->digital_write(0);
  delay(5);
  this->reset_pin_->digital_write(1);
  delay(5);
}

void IRAM_ATTR Transceiver::notify_interrupt(InterruptContext *context) {
  BaseType_t higher_priority_task_woken = pdFALSE;
  xTaskNotifyFromISR(context->task, context->value, eSetBits, &higher_priority_task_woken);
  portYIELD_FROM_ISR(higher_priority_task_woken);
}

void Transceiver::dump_config() {
  ESP_LOGCONFIG(TAG, "Transceiver: %s", this->get_name());
  LOG_PIN("Reset Pin: ", this->reset_pin_);
  LOG_PIN("DIO1 Pin: ", this->dio1_pin_);
}

}  // namespace wmbus_radio
}  // namespace esphome
