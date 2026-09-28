#include "transceiver_sx1276.h"

#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_radio {

static const char *TAG = "SX1276";
static constexpr uint32_t F_OSC = 32000000;

enum class SX1276::Register : uint8_t {
  FIFO = 0x00,
  OP_MODE = 0x01,
  BITRATE_MSB = 0x02,
  FDEV_MSB = 0x04,
  FRF_MSB = 0x06,
  RX_CONFIG = 0x0D,
  RSSI_CONFIG = 0x0E,
  RSSI_VALUE = 0x11,
  RX_BW = 0x12,
  PREAMBLE_DETECT = 0x1F,
  OSC = 0x24,
  PREAMBLE_MSB = 0x25,
  SYNC_CONFIG = 0x27,
  PACKET_CONFIG1 = 0x30,
  PAYLOAD_LENGTH = 0x32,
  IRQ_FLAGS2 = 0x3F,
  DIO_MAPPING1 = 0x40,
  VERSION = 0x42,
  BITRATE_FRAC = 0x5D,
};

void SX1276::setup() {
  this->reset_pin_->setup();
  this->dio1_pin_->setup();
  this->spi_setup();

  ESP_LOGV(TAG, "Setting up SX1276");
  ESP_LOGVV(TAG, "reset");
  this->reset();

  ESP_LOGVV(TAG, "checking silicon revision");
  uint8_t revision = this->read_register(Register::VERSION);
  ESP_LOGVV(TAG, "revision: %02X", revision);
  if (revision < 0x11 || revision > 0x13) {
    ESP_LOGE(TAG, "Invalid silicon revision: %02X", revision);
    this->mark_failed();
    return;
  }

  ESP_LOGVV(TAG, "setting radio frequency");
  constexpr uint32_t frequency = 868950000;
  constexpr uint32_t frf = ((uint64_t) frequency * (1 << 19)) / F_OSC;
  this->write_register(Register::FRF_MSB, {BYTE(frf, 2), BYTE(frf, 1), BYTE(frf, 0)});

  ESP_LOGVV(TAG, "setting radio bandwidth");
  this->write_register(Register::RX_BW, {2, 2});

  ESP_LOGVV(TAG, "set frequency deviation");
  constexpr uint16_t freq_dev = 50000;
  constexpr uint16_t frd = ((uint64_t) freq_dev * (1 << 19)) / F_OSC;
  this->write_register(Register::FDEV_MSB, {BYTE(frd, 1), BYTE(frd, 0)});

  ESP_LOGVV(TAG, "set bitrate");
  constexpr uint32_t bitrate = 100000;
  constexpr uint32_t br = (F_OSC << 4) / bitrate;  // 1/16 steps
  this->write_register(Register::BITRATE_FRAC, {static_cast<uint8_t>(br & 0x0F)});
  this->write_register(Register::BITRATE_MSB, {BYTE(br >> 4, 1), BYTE(br >> 4, 0)});

  ESP_LOGVV(TAG, "set preamble length");
  constexpr uint16_t preamble_length = 32 / 8;
  this->write_register(Register::PREAMBLE_MSB, {BYTE(preamble_length, 1), BYTE(preamble_length, 0)});

  ESP_LOGVV(TAG, "enable preamble detection");
  constexpr uint8_t preamble_detection = (1 << 7) | (1 << 5) | 0x0A;
  this->write_register(Register::PREAMBLE_DETECT, {preamble_detection});

  ESP_LOGVV(TAG, "enable auto agc/afc");
  constexpr uint8_t agc_afc = (1 << 4) | (1 << 3) | 0b110;
  this->write_register(Register::RX_CONFIG, {agc_afc});

  ESP_LOGVV(TAG, "disable clock output");
  this->write_register(Register::OSC, {0b111});

  ESP_LOGVV(TAG, "set sync word and reverse preamble polarity");
  constexpr uint8_t reverse_preamble_sync_bytes = (1 << 5) | (1 << 4) | (2 - 1);
  this->write_register(Register::SYNC_CONFIG, {reverse_preamble_sync_bytes, 0x54, 0x3D});

  ESP_LOGVV(TAG, "disable crc check/fixed packet length");
  this->write_register(Register::PACKET_CONFIG1, {0});

  ESP_LOGVV(TAG, "set unlimited packet mode/zero length");
  this->write_register(Register::PAYLOAD_LENGTH, {0});

  ESP_LOGVV(TAG, "set fifo empty flag on DIO1");
  this->write_register(Register::DIO_MAPPING1, {0b01 << 4});

  ESP_LOGVV(TAG, "set RRSI smoothing");
  this->write_register(Register::RSSI_CONFIG, {0b111});

  ESP_LOGI(TAG, "SX1276 setup done");
}

void SX1276::start_receiver(TaskHandle_t receiver_task) {
  this->data_ready_interrupt_ = {receiver_task, 1};
  this->dio1_pin_->attach_interrupt(&Transceiver::notify_interrupt, &this->data_ready_interrupt_,
                                    gpio::INTERRUPT_FALLING_EDGE);
}

bool SX1276::read(std::span<uint8_t> buffer, TickType_t first_byte_timeout) {
  if (this->dio1_pin_->digital_read() && !ulTaskNotifyTake(pdTRUE, first_byte_timeout))
    return false;

  while (!buffer.empty()) {
    if (this->dio1_pin_->digital_read()) {
      if (!ulTaskNotifyTake(pdTRUE, SINGLE_BYTE_TIMEOUT))
        return false;
    }

    this->enable();
    this->transfer_byte(static_cast<uint8_t>(Register::FIFO));
    while (!buffer.empty() && !this->dio1_pin_->digital_read()) {
      buffer.front() = this->transfer_byte(0x00);
      buffer = buffer.subspan(1);
    }
    this->disable();
  }

  return true;
}

void SX1276::reset_receiver() {
  this->write_register(Register::OP_MODE, {0b001});
  delay(5);
  this->write_register(Register::IRQ_FLAGS2, {1 << 4});
  ulTaskNotifyTake(pdTRUE, 0);
  this->write_register(Register::OP_MODE, {0b101});
  delay(5);
}

int8_t SX1276::get_rssi() {
  uint8_t rssi = this->read_register(Register::RSSI_VALUE);
  return (int8_t) (-rssi / 2);
}

const char *SX1276::get_name() { return TAG; }

uint8_t SX1276::read_register(Register address) {
  std::array<uint8_t, 2> transaction{static_cast<uint8_t>(address), 0x00};
  this->spi_transaction(transaction);
  return transaction[1];
}

void SX1276::write_register(Register address, std::initializer_list<uint8_t> data) {
  this->enable();
  this->write_byte(static_cast<uint8_t>(address) | 0x80);
  this->write_array(data.begin(), data.size());
  this->disable();
}

}  // namespace wmbus_radio
}  // namespace esphome
