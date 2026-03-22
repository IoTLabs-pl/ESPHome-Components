#pragma once
#include "transceiver.h"

namespace esphome {
namespace wmbus_radio {
class SX1276 : public RadioTransceiver {
 public:
  void setup() override;
  bool read(uint8_t *buffer, size_t length) override;
  void restart_rx() override;
  int8_t get_rssi() override;
  const char *get_name() override;
};
}  // namespace wmbus_radio
}  // namespace esphome
