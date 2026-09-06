#pragma once

#include "transceiver.h"

namespace esphome {
namespace wmbus_radio {

class SX1276 final : public Transceiver {
 public:
  void setup() override;
  void start_receiver(TaskHandle_t receiver_task) override;
  void reset_receiver() override;
  bool read(std::span<uint8_t> buffer, TickType_t first_byte_timeout) override;
  int8_t get_rssi() override;
  const char *get_name() override;

 private:
  enum class Register : uint8_t;
  InterruptContext data_ready_interrupt_;

  uint8_t read_register(Register address);
  void write_register(Register address, std::initializer_list<uint8_t> data);
};

}  // namespace wmbus_radio
}  // namespace esphome
