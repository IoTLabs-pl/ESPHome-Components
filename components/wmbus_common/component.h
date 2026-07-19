#pragma once
#include "esphome/core/component.h"

namespace esphome {
namespace wmbus_common {
class WMBusCommon : public Component {
 public:
  void dump_config() override;
};
}  // namespace wmbus_common
}  // namespace esphome