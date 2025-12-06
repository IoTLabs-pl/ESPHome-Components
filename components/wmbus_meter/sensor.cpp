#include "sensor.h"

namespace esphome {
namespace wmbus_meter {
static const char *TAG = "wmbus_meter.sensor";

void Sensor::handle_update() {
  auto val = this->parent_->get_numeric_field(this->field_name);
  if (!val.has_value())
    return;

  this->publish_state(*val);

  if (this->dynamic_decimals_) {
    auto str = std::to_string(this->state);

    while (str.back() == '0')
      str.pop_back();

    auto decimals = 0;
    while (str.back() != '.') {
      str.pop_back();
      decimals++;
    }

    this->set_accuracy_decimals(decimals);
  }
}

void Sensor::set_dynamic_decimals(bool dynamic_decimals) { this->dynamic_decimals_ = dynamic_decimals; }

}  // namespace wmbus_meter
}  // namespace esphome
