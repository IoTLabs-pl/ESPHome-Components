#pragma once

#include "transceiver.h"

namespace esphome {
namespace wmbus_radio {

class SX1262 final : public Transceiver {
 public:
  enum class TcxoVoltage : uint8_t {
    V1_6 = 0x00,
    V1_7 = 0x01,
    V1_8 = 0x02,
    V2_2 = 0x03,
    V2_4 = 0x04,
    V2_7 = 0x05,
    V3_0 = 0x06,
    V3_3 = 0x07,
    NONE = 0xFF,
  };

  void setup() override;
  void dump_config() override;
  void start_receiver(TaskHandle_t receiver_task) override;
  void reset_receiver() override;
  bool read(std::span<uint8_t> buffer, TickType_t first_byte_timeout) override;
  int8_t get_rssi() override;
  const char *get_name() override;

  void set_busy_pin(InternalGPIOPin *busy_pin) { this->busy_pin_ = busy_pin; }
  void set_tcxo_voltage(TcxoVoltage tcxo_voltage) { this->tcxo_voltage_ = tcxo_voltage; }
  void set_rf_switch(bool rf_switch) { this->rf_switch_ = rf_switch; }
  void set_use_dcdc(bool use_dcdc) { this->use_dcdc_ = use_dcdc; }
  void set_rx_boost(bool rx_boost) { this->rx_boost_ = rx_boost; }

 private:
  enum class Register : uint16_t;
  enum class Opcode : uint8_t;
  enum class Interrupt : uint32_t {
    SYNC_WORD = 1 << 0,
    BUSY_READY = 1 << 1,
  };

  InternalGPIOPin *busy_pin_;
  TcxoVoltage tcxo_voltage_;
  bool rf_switch_;
  bool use_dcdc_;
  bool rx_boost_;
  uint8_t rx_read_offset_{0};
  InterruptContext sync_word_interrupt_;
  InterruptContext busy_ready_interrupt_;

  bool configure_chip();
  bool wait_for_busy();
  bool wait_for(Interrupt interrupt, TickType_t timeout);

  void command(Opcode opcode, std::initializer_list<uint8_t> parameters, std::span<uint8_t> response = {});
  void read_registers(Register address, std::span<uint8_t> data);
  void write_register(Register address, uint8_t value);

  uint8_t available_bytes();
  size_t read_buffer(std::span<uint8_t> target);
};

}  // namespace wmbus_radio
}  // namespace esphome
