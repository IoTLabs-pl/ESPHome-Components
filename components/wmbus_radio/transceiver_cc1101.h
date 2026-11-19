#pragma once

#include "transceiver.h"

namespace esphome {
namespace wmbus_radio {

// TI CC1101 868 MHz receiver for the IoTLabs wM-Bus radio Transceiver API.
// Requires both GDO2 (SYNC asserted, rising IRQ) and GDO0 (FIFO threshold).
class CC1101 final : public Transceiver {
 public:
  void setup() override;
  void dump_config() override;
  void start_receiver(TaskHandle_t receiver_task) override;
  void reset_receiver() override;
  bool read(std::span<uint8_t> buffer, TickType_t first_byte_timeout) override;
  int8_t get_rssi() override;
  const char *get_name() override { return "CC1101"; }

  void set_frequency(uint32_t hz) { this->frequency_hz_ = hz; }
  void set_gdo0_pin(InternalGPIOPin *pin) { this->gdo0_pin_ = pin; }

 private:
  uint32_t frequency_hz_{868950000};
  InternalGPIOPin *gdo0_pin_{nullptr};
  InterruptContext data_ready_interrupt_{};
  uint8_t last_raw_rssi_{0};
  bool awaiting_start_{true};
  bool rssi_captured_{false};
  uint8_t strobe(uint8_t cmd);
  uint8_t read_reg(uint8_t addr, bool status = false);
  void write_reg(uint8_t addr, uint8_t value);
  void read_fifo(uint8_t *buf, size_t count);
};

}  // namespace wmbus_radio
}  // namespace esphome
