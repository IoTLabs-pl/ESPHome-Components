#pragma once

#include "esphome/core/component.h"
#include "esphome/components/http_request/http_request.h"
#include "esphome/components/wmbus_radio/component.h"

#include <string>

namespace esphome {
namespace iotlabs_meters {

using Radio = wmbus_radio::Radio;
using Frame = wmbus_radio::Frame;

class IoTLabsMetersClient : public Component {
 public:
  void set_base_url(const char *v) { url_ = v; }
  void set_auth_header(const char *v) { auth_header_ = v; }
  void set_http_request(http_request::HttpRequestComponent *v) { http_ = v; }
  void set_wmbus_radio(Radio *radio) {
    radio->add_frame_handler([this](Frame *frame) { on_frame(frame); });
  }

  void set_max_delivery_attempts(uint8_t v) { max_delivery_attempts_ = v; }
  void set_delivery_retry_interval_ms(uint32_t v) { delivery_retry_interval_ms_ = v; }

  void dump_config() override;

 private:
  static constexpr const char *TAG = "iotlabs";

  http_request::HttpRequestComponent *http_{nullptr};

  const char *url_{};
  const char *auth_header_{};

  uint8_t max_delivery_attempts_;
  uint32_t delivery_retry_interval_ms_;

  void on_frame(Frame *frame);
  void deliver_frame(const std::string &body, uint8_t attempts_left);
};

}  // namespace iotlabs_meters
}  // namespace esphome
