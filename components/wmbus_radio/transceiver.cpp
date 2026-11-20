#include "transceiver.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_radio {

ESPHOME_LOG_TAG(TAG, "wmbus.transceiver");

void IRAM_ATTR Transceiver::notify_interrupt(InterruptContext *context) {
  BaseType_t higher_priority_task_woken = pdFALSE;
  xTaskNotifyFromISR(context->task, context->value, eSetBits, &higher_priority_task_woken);
  portYIELD_FROM_ISR(higher_priority_task_woken);
}

void Transceiver::dump_config() { ESP_LOGCONFIG(TAG, "Transceiver: %s", this->get_name()); }

}  // namespace wmbus_radio
}  // namespace esphome
