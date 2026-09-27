#include "component.h"

#include "esphome/components/wmbus_common/meter.h"

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_common {
static const char *TAG = "wmbus_common";

void WMBusCommon::dump_config() {
  ESP_LOGCONFIG(TAG, "wM-Bus Component:");
  ESP_LOGCONFIG(TAG, "  wmbusmeters version: %s", WMBUSMETERS_VERSION);
  ESP_LOGCONFIG(TAG, "  Compiled drivers:");
  for (const wmbus::DriverSpec *spec : wmbus::registered_drivers)
    ESP_LOGCONFIG(TAG, "    - %s", spec->name);
}

}  // namespace wmbus_common
}  // namespace esphome
