#pragma once
#include <array>
#include <optional>
#include <string>

#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#include "esphome/components/wmbus_radio/component.h"
#include "esphome/components/wmbus_common/meter.h"

namespace esphome {
namespace wmbus_meter {
class Meter : public Component {
 public:
  // A null spec is `type: auto`: the driver is detected from the first telegram.
  void set_driver(const wmbus::DriverSpec *spec, uint32_t id, std::optional<std::array<uint8_t, 16>> key);
  void set_radio(wmbus_radio::Radio *radio);

  void dump_config() override;

  void on_telegram(std::function<void()> &&callback);

  std::string get_id() const;

  std::string as_json();
  optional<std::string> get_string_field(std::string field_name);
  optional<float> get_numeric_field(std::string field_name);

 protected:
  wmbus::MeterState state_;

  wmbus_radio::Radio *radio;

  CallbackManager<void()> on_telegram_callback_manager;

  virtual void handle_frame(wmbus_radio::Frame *frame);
};
}  // namespace wmbus_meter
}  // namespace esphome
