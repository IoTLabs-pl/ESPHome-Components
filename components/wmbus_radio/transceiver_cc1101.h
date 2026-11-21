#pragma once

#include "transceiver.h"

namespace esphome {
namespace wmbus_radio {

class CC1101 final : public Transceiver {
 public:
  enum class Register : uint8_t {
    IOCFG2 = 0x00,
    IOCFG0 = 0x02,
    SYNC1 = 0x04,
    PKTCTRL1 = 0x07,
    FSCTRL1 = 0x0B,
    FREQ2 = 0x0D,
    MDMCFG4 = 0x10,
    DEVIATN = 0x15,
    MCSM0 = 0x18,
    FREND1 = 0x21,
    FSCAL3 = 0x23,
    TEST2 = 0x2C,
    VERSION = 0x31,
    RSSI = 0x34,
    RXBYTES = 0x3B,
    FIFO = 0x3F,
  };

  void setup() override;
  void dump_config() override;
  void start_receiver(TaskHandle_t receiver_task) override;
  void reset_receiver() override;
  bool read(std::span<uint8_t> buffer, TickType_t first_byte_timeout) override;
  int8_t get_rssi() override;
  const char *get_name() override;

  void set_gdo_pin(InternalGPIOPin *gdo_pin, Register gdo_config) {
    this->gdo_pin_ = gdo_pin;
    this->gdo_config_ = gdo_config;
  }

 private:
  enum class Strobe : uint8_t;
  InternalGPIOPin *gdo_pin_;
  Register gdo_config_;
  InterruptContext sync_word_interrupt_;

  void strobe(Strobe command);
  uint8_t read_register(Register address);
  uint8_t read_stable_register(Register address);
  void write_register(Register address, std::initializer_list<uint8_t> data);

  uint8_t available_bytes();
  size_t read_fifo(std::span<uint8_t> target);
};

}  // namespace wmbus_radio
}  // namespace esphome
