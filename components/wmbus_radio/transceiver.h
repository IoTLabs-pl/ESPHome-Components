#pragma once

#include <cstdint>
#include <span>

#include "esphome/components/spi/spi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define BYTE(x, n) ((uint8_t) (x >> (n * 8)))

namespace esphome {
namespace wmbus_radio {

class Transceiver : public Component,
                    public spi::SPIDevice<spi::BIT_ORDER_MSB_FIRST, spi::CLOCK_POLARITY_LOW, spi::CLOCK_PHASE_LEADING,
                                          spi::DATA_RATE_8MHZ> {
 public:
  static constexpr TickType_t SINGLE_BYTE_TIMEOUT = pdMS_TO_TICKS(2);

  virtual void setup() override = 0;
  void dump_config() override;

  float get_setup_priority() const override { return setup_priority::HARDWARE - 10.0f; }

  // Called from the receiver task before its first reset_receiver().
  virtual void start_receiver(TaskHandle_t receiver_task) = 0;
  virtual void reset_receiver() = 0;
  // Fills the whole buffer; false when the first byte does not come within first_byte_timeout
  // or a later one within SINGLE_BYTE_TIMEOUT.
  virtual bool read(std::span<uint8_t> buffer, TickType_t first_byte_timeout) = 0;
  virtual int8_t get_rssi() = 0;
  virtual const char *get_name() = 0;

  void set_reset_pin(InternalGPIOPin *reset_pin);
  void set_dio1_pin(InternalGPIOPin *dio1_pin);

 protected:
  struct InterruptContext {
    TaskHandle_t task;
    uint32_t value;
  };

  InternalGPIOPin *reset_pin_;
  InternalGPIOPin *dio1_pin_;

  void reset();
  static void IRAM_ATTR notify_interrupt(InterruptContext *context);

  void spi_transaction(std::span<uint8_t> data) {
    this->enable();
    this->transfer_array(data.data(), data.size());
    this->disable();
  }
};

}  // namespace wmbus_radio
}  // namespace esphome
