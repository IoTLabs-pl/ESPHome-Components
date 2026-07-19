#include "component.h"

#include "meters.h"

#include "esphome/core/defines.h"
#include "esphome/core/log.h"

extern "C" {
void register_wmbus_drivers();
}

namespace esphome {
namespace wmbus_common {
static const char *TAG = "wmbus_common";

std::vector<DriverInfo *> sorted_drivers() {
  register_wmbus_drivers();
  auto drivers = allDrivers();
  std::sort(drivers.begin(), drivers.end(),
            [](DriverInfo *a, DriverInfo *b) { return a->name().str() < b->name().str(); });
  return drivers;
}

void WMBusCommon::dump_config() {
  ESP_LOGCONFIG(TAG, "wM-Bus Component:");
  ESP_LOGCONFIG(TAG, "  wmbusmeters version: %s", WMBUSMETERS_TAG);
  ESP_LOGCONFIG(TAG, "  Loaded drivers:");
  for (auto &driver : sorted_drivers())
    ESP_LOGCONFIG(TAG, ("    - " + driver->name().str()).c_str());
}

}  // namespace wmbus_common
}  // namespace esphome