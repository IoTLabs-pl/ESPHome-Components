#pragma once
#include "esphome/core/component.h"

namespace esphome {
namespace wmbus_common {
extern const std::vector<std::string> driver_names;

class WMBusCommon : public Component {
 public:
  void dump_config() override;
};
}  // namespace wmbus_common
}  // namespace esphome