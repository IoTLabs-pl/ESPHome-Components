#include "sensor.h"

namespace esphome {
namespace wmbus_meter {

void Sensor::handle_update() {
  auto val = this->parent_->get_numeric_field(this->field_name);
  if (val.has_value())
    this->publish_state(*val);
}

}  // namespace wmbus_meter
}  // namespace esphome
