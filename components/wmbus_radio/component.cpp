#include "component.h"

#include "freertos/task.h"

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_radio {
static const char *TAG = "wmbus";

void Radio::setup() {
  if (this->transceiver_->is_failed()) {
    ESP_LOGE(TAG, "Transceiver failed, not starting the receiver");
    this->mark_failed();
    return;
  }

  if (xTaskCreate((TaskFunction_t) this->receiver_task, "radio_recv", 3 * 1024, this, 2, nullptr) != pdPASS) {
    ESP_LOGE(TAG, "Failed to create receiver task");
    this->mark_failed();
    return;
  }

  ESP_LOGI(TAG, "Receiver task created");
}

void Radio::loop() {
  std::unique_ptr<Packet> packet{this->packet_queue_.pop()};
  if (!packet) {
    this->disable_loop();
    return;
  }

  this->on_packet_callback_manager_(packet.get());

  auto frame = packet->convert_to_frame();

  if (!frame)
    return;

  ESP_LOGI(TAG, "Frame created (%zu bytes) [RSSI: %d, mode:%s%s]", frame->data().size(), frame->rssi(),
           toString(frame->link_mode()), toString(frame->block_type()));

  this->on_frame_callback_manager_(&frame.value());

  ESP_LOGI(TAG, "Telegram handled by %d handlers", frame->frame_handlers_count());
}

void Radio::receive_frame() {
  this->transceiver_->reset_receiver();

  auto packet = std::make_unique<Packet>();

  if (!this->transceiver_->read(packet->head(), pdMS_TO_TICKS(60000))) {
    ESP_LOGV(TAG, "Failed to read packet head");
    return;
  }

  const auto rest = packet->rest();
  if (rest.empty()) {
    ESP_LOGV(TAG, "Received invalid packet head: [%s]", format_hex_pretty(packet->get_raw_data()).c_str());
    return;
  }

  packet->set_rssi(this->transceiver_->get_rssi());

  if (!this->transceiver_->read(rest, Transceiver::SINGLE_BYTE_TIMEOUT)) {
    ESP_LOGW(TAG, "Failed to read data");
    return;
  }

  if (this->packet_queue_.push(packet.get())) {
    packet.release();
    this->enable_loop_soon_any_context();
    ESP_LOGV(TAG, "Queue items: %zu", this->packet_queue_.size());
    ESP_LOGV(TAG, "Queue send success");
  } else {
    ESP_LOGW(TAG, "Queue send failed");
  }
}

void Radio::receiver_task(Radio *arg) {
  arg->transceiver_->start_receiver(xTaskGetCurrentTaskHandle());
  while (true)
    arg->receive_frame();
}

}  // namespace wmbus_radio
}  // namespace esphome
