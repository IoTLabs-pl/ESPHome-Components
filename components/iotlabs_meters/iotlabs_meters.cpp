#include "iotlabs_meters.h"
#include "esphome/core/log.h"
#include "esphome/core/helpers.h"

#include <ArduinoJson.h>

#include <cstring>

namespace esphome {
namespace iotlabs_meters {

void IoTLabsMetersClient::dump_config() { ESP_LOGCONFIG(TAG, "IoTLabs Meters"); }

void IoTLabsMetersClient::on_frame(Frame *frame) {
  const auto &data = frame->data();

  JsonDocument doc;
  doc["payload"] = base64_encode(data);
  doc["mode"] = toString(frame->link_mode());
  doc["rssi"] = (int) frame->rssi();
  auto format = toString(frame->block_type());
  if (strlen(format) > 0)
    doc["format"] = format;

  std::string body;
  serializeJson(doc, body);

  deliver_frame(body, max_delivery_attempts_);
}

void IoTLabsMetersClient::deliver_frame(const std::string &body, uint8_t attempts_left) {
  if (attempts_left == 0) {
    ESP_LOGW(TAG, "frame delivery abandoned");
    return;
  }

  auto schedule_retry = [this, body, attempts_left]() {
    ESP_LOGD(TAG, "retrying frame delivery in %u s (%u attempts left)", delivery_retry_interval_ms_ / 1000u,
             attempts_left - 1);
    set_timeout(delivery_retry_interval_ms_, [this, body, attempts_left]() { deliver_frame(body, attempts_left - 1); });
  };

  auto resp = http_->post(url_, body,
                          std::vector{
                              http_request::Header{"Content-Type", "application/json"},
                              http_request::Header{"Authorization", auth_header_},
                          });

  int code = resp ? resp->status_code : -1;
  if (resp)
    resp->end();

  if (code >= 200 && code < 300) {
    ESP_LOGD(TAG, "frame delivered");
    return;
  }

  ESP_LOGW(TAG, "POST ingest -> %d", code);

  schedule_retry();
}

}  // namespace iotlabs_meters
}  // namespace esphome
