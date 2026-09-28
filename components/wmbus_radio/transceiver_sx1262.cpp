#include "transceiver_sx1262.h"

#include <algorithm>
#include <array>
#include <string_view>

#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_radio {

static const char *TAG = "SX1262";
static constexpr uint32_t F_XTAL = 32000000;

enum class SX1262::Register : uint16_t {
  VERSION_STRING = 0x0320,
  RX_TX_PAYLOAD_LEN = 0x06BB,
  SYNC_WORD_0 = 0x06C0,
  SYNC_WORD_1 = 0x06C1,
  RX_ADDR_POINTER = 0x0803,
  RX_GAIN = 0x08AC,
};

enum class SX1262::Opcode : uint8_t {
  CLEAR_IRQ_STATUS = 0x02,
  CLEAR_DEVICE_ERRORS = 0x07,
  SET_DIO_IRQ_PARAMS = 0x08,
  WRITE_REGISTER = 0x0D,
  GET_PACKET_STATUS = 0x14,
  GET_DEVICE_ERRORS = 0x17,
  READ_REGISTER = 0x1D,
  READ_BUFFER = 0x1E,
  SET_STANDBY = 0x80,
  SET_RX = 0x82,
  SET_RF_FREQUENCY = 0x86,
  CALIBRATE = 0x89,
  SET_PACKET_TYPE = 0x8A,
  SET_MODULATION_PARAMS = 0x8B,
  SET_PACKET_PARAMS = 0x8C,
  SET_BUFFER_BASE_ADDRESS = 0x8F,
  SET_REGULATOR_MODE = 0x96,
  SET_DIO3_AS_TCXO_CTRL = 0x97,
  CALIBRATE_IMAGE = 0x98,
  SET_DIO2_AS_RF_SWITCH_CTRL = 0x9D,
};

void SX1262::setup() {
  this->busy_pin_->setup();
  this->reset_pin_->setup();
  this->dio1_pin_->setup();
  this->busy_ready_interrupt_ = {xTaskGetCurrentTaskHandle(), static_cast<uint32_t>(Interrupt::BUSY_READY)};
  this->busy_pin_->attach_interrupt(&Transceiver::notify_interrupt, &this->busy_ready_interrupt_,
                                    gpio::INTERRUPT_FALLING_EDGE);
  this->spi_setup();

  if (!this->configure_chip()) {
    this->busy_pin_->detach_interrupt();
    this->mark_failed();
  }
}

bool SX1262::configure_chip() {
  ESP_LOGV(TAG, "Setting up SX1262");
  this->reset();
  if (!this->wait_for_busy()) {
    ESP_LOGE(TAG, "No response, BUSY stays high after reset");
    return false;
  }

  std::array<uint8_t, 16> version;
  this->read_registers(Register::VERSION_STRING, version);
  if (!std::string_view(reinterpret_cast<const char *>(version.data()), version.size()).starts_with("SX126")) {
    char hex[format_hex_pretty_size(version.size())];
    ESP_LOGE(TAG, "Unexpected version string: %s", format_hex_pretty_to(hex, version));
    return false;
  }

  ESP_LOGVV(TAG, "standby on RC oscillator");
  this->command(Opcode::SET_STANDBY, {0x00});

  if (this->tcxo_voltage_ != TcxoVoltage::NONE) {
    ESP_LOGVV(TAG, "power TCXO from DIO3");
    constexpr uint32_t startup_delay_us = 5000;
    constexpr uint32_t startup_delay = startup_delay_us * 1000 / 15625;  // 15.625 us steps
    this->command(Opcode::SET_DIO3_AS_TCXO_CTRL, {static_cast<uint8_t>(this->tcxo_voltage_), BYTE(startup_delay, 2),
                                                  BYTE(startup_delay, 1), BYTE(startup_delay, 0)});
  }

  ESP_LOGVV(TAG, "calibrate blocks and image for 863-870 MHz");
  this->command(Opcode::CALIBRATE, {0x7F});
  this->command(Opcode::CALIBRATE_IMAGE, {0xD7, 0xDB});

  std::array<uint8_t, 2> errors;
  this->command(Opcode::GET_DEVICE_ERRORS, {}, errors);
  // Power-up raises XOSC_START_ERR when a TCXO is used.
  const uint16_t device_errors = encode_uint16(errors[0], errors[1]) & ~(1 << 5);
  if (device_errors != 0) {
    ESP_LOGE(TAG, "Calibration failed (device errors 0x%04X), check tcxo_voltage", device_errors);
    return false;
  }
  this->command(Opcode::CLEAR_DEVICE_ERRORS, {0x00, 0x00});

  if (this->rf_switch_) {
    ESP_LOGVV(TAG, "drive RF switch from DIO2");
    this->command(Opcode::SET_DIO2_AS_RF_SWITCH_CTRL, {0x01});
  }

  ESP_LOGVV(TAG, "set %s regulator", this->use_dcdc_ ? "DC-DC" : "LDO");
  this->command(Opcode::SET_REGULATOR_MODE, {this->use_dcdc_});

  ESP_LOGVV(TAG, "set GFSK packet type");
  this->command(Opcode::SET_PACKET_TYPE, {0x00});

  ESP_LOGVV(TAG, "set radio frequency");
  constexpr uint32_t frequency = 868950000;
  constexpr uint32_t frf = (uint64_t{frequency} << 25) / F_XTAL;
  this->command(Opcode::SET_RF_FREQUENCY, {BYTE(frf, 3), BYTE(frf, 2), BYTE(frf, 1), BYTE(frf, 0)});

  ESP_LOGVV(TAG, "set bitrate, gaussian BT 0.5 shaping, 234.3 kHz bandwidth and frequency deviation");
  constexpr uint32_t bitrate = 100000;
  constexpr uint32_t br = 32 * F_XTAL / bitrate;
  constexpr uint32_t freq_dev = 50000;
  constexpr uint32_t fdev = ((uint64_t{freq_dev} << 25) + F_XTAL / 2) / F_XTAL;
  this->command(Opcode::SET_MODULATION_PARAMS,
                {BYTE(br, 2), BYTE(br, 1), BYTE(br, 0), 0x09, 0x0A, BYTE(fdev, 2), BYTE(fdev, 1), BYTE(fdev, 0)});

  ESP_LOGVV(TAG, "set 16-bit preamble and sync word, fixed 255-byte length, no CRC, no whitening");
  this->command(Opcode::SET_PACKET_PARAMS, {0x00, 0x10, 0x04, 0x10, 0x00, 0x00, 0xFF, 0x01, 0x00});
  this->command(Opcode::SET_BUFFER_BASE_ADDRESS, {0x00, 0x00});

  ESP_LOGVV(TAG, "set sync word");
  this->write_register(Register::SYNC_WORD_0, 0x54);
  this->write_register(Register::SYNC_WORD_1, 0x3D);

  ESP_LOGVV(TAG, "set %s RX gain", this->rx_boost_ ? "boosted" : "power saving");
  this->write_register(Register::RX_GAIN, this->rx_boost_ ? 0x96 : 0x94);

  ESP_LOGVV(TAG, "route SyncWordValid IRQ to DIO1");
  this->command(Opcode::SET_DIO_IRQ_PARAMS, {0x00, 0x08, 0x00, 0x08, 0x00, 0x00, 0x00, 0x00});

  ESP_LOGI(TAG, "SX1262 setup done");
  return true;
}

void SX1262::dump_config() {
  Transceiver::dump_config();
  LOG_PIN("Busy Pin: ", this->busy_pin_);
  if (this->tcxo_voltage_ != TcxoVoltage::NONE) {
    ESP_LOGCONFIG(TAG, "TCXO voltage code: 0x%02X", static_cast<uint8_t>(this->tcxo_voltage_));
  }
  ESP_LOGCONFIG(TAG, "DIO2 as RF switch: %s", YESNO(this->rf_switch_));
  ESP_LOGCONFIG(TAG, "Regulator: %s", this->use_dcdc_ ? "DC-DC" : "LDO");
  ESP_LOGCONFIG(TAG, "RX gain: %s", this->rx_boost_ ? "boosted" : "power saving");
}

void SX1262::start_receiver(TaskHandle_t receiver_task) {
  this->busy_ready_interrupt_.task = receiver_task;
  this->sync_word_interrupt_ = {receiver_task, static_cast<uint32_t>(Interrupt::SYNC_WORD)};
  this->dio1_pin_->attach_interrupt(&Transceiver::notify_interrupt, &this->sync_word_interrupt_,
                                    gpio::INTERRUPT_RISING_EDGE);
}

void SX1262::reset_receiver() {
  this->command(Opcode::SET_STANDBY, {0x01});
  this->rx_read_offset_ = 0;
  this->command(Opcode::SET_BUFFER_BASE_ADDRESS, {0x00, 0x00});
  this->write_register(Register::RX_TX_PAYLOAD_LEN, 0xFF);
  this->command(Opcode::CLEAR_IRQ_STATUS, {0xFF, 0xFF});
  ulTaskNotifyValueClear(nullptr, static_cast<uint32_t>(Interrupt::SYNC_WORD));
  this->command(Opcode::SET_RX, {0xFF, 0xFF, 0xFF});
}

bool SX1262::read(std::span<uint8_t> buffer, TickType_t first_byte_timeout) {
  // SyncWordValid holds DIO1 high until reset_receiver() clears it.
  if (!this->dio1_pin_->digital_read() && !this->wait_for(Interrupt::SYNC_WORD, first_byte_timeout))
    return false;

  TickType_t last_data = xTaskGetTickCount();
  while (!buffer.empty()) {
    const size_t bytes_read = this->read_buffer(buffer);
    if (bytes_read != 0) {
      buffer = buffer.subspan(bytes_read);
      last_data = xTaskGetTickCount();
    } else if (xTaskGetTickCount() - last_data > SINGLE_BYTE_TIMEOUT) {
      return false;
    } else {
      vTaskDelay(1);
    }
  }
  return true;
}

int8_t SX1262::get_rssi() {
  std::array<uint8_t, 3> packet_status;
  this->command(Opcode::GET_PACKET_STATUS, {}, packet_status);
  const auto [rx_status, rssi_sync, rssi_average] = packet_status;
  return -static_cast<int8_t>(rssi_sync / 2);
}

const char *SX1262::get_name() { return TAG; }

bool SX1262::wait_for_busy() {
  ulTaskNotifyValueClear(nullptr, static_cast<uint32_t>(Interrupt::BUSY_READY));
  while (this->busy_pin_->digital_read()) {
    if (!this->wait_for(Interrupt::BUSY_READY, pdMS_TO_TICKS(100)) && this->busy_pin_->digital_read()) {
      ESP_LOGW(TAG, "BUSY wait timeout");
      return false;
    }
  }
  return true;
}

bool SX1262::wait_for(Interrupt interrupt, TickType_t timeout) {
  const auto mask = static_cast<uint32_t>(interrupt);
  uint32_t notified;
  while (xTaskNotifyWait(0, mask, &notified, timeout) == pdTRUE) {
    if (notified & mask)
      return true;
  }
  return false;
}

void SX1262::command(Opcode opcode, std::initializer_list<uint8_t> parameters, std::span<uint8_t> response) {
  this->wait_for_busy();
  this->enable();
  this->write_byte(static_cast<uint8_t>(opcode));
  this->write_array(parameters.begin(), parameters.size());
  if (!response.empty()) {
    // Reads return the status byte before the data.
    this->write_byte(0x00);
    this->read_array(response.data(), response.size());
  }
  this->disable();
}

void SX1262::read_registers(Register address, std::span<uint8_t> data) {
  const auto raw = static_cast<uint16_t>(address);
  this->command(Opcode::READ_REGISTER, {BYTE(raw, 1), BYTE(raw, 0)}, data);
}

void SX1262::write_register(Register address, uint8_t value) {
  const auto raw = static_cast<uint16_t>(address);
  this->command(Opcode::WRITE_REGISTER, {BYTE(raw, 1), BYTE(raw, 0), value});
}

uint8_t SX1262::available_bytes() {
  std::array<uint8_t, 1> rx_pointer;
  this->read_registers(Register::RX_ADDR_POINTER, rx_pointer);
  return rx_pointer[0] - this->rx_read_offset_;
}

size_t SX1262::read_buffer(std::span<uint8_t> target) {
  target = target.first(std::min<size_t>(this->available_bytes(), target.size()));
  if (target.empty())
    return 0;

  this->command(Opcode::READ_BUFFER, {this->rx_read_offset_}, target);
  this->rx_read_offset_ += target.size();
  // The data buffer is a 256-byte ring. Keeping the fixed payload length just behind the read offset lets
  // a frame run past 255 bytes while the chip never overwrites what is still unread.
  this->write_register(Register::RX_TX_PAYLOAD_LEN, this->rx_read_offset_ - 1);
  return target.size();
}

}  // namespace wmbus_radio
}  // namespace esphome
