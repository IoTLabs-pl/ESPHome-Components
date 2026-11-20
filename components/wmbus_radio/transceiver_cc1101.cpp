#include "transceiver_cc1101.h"
#include <algorithm>
#include <utility>
#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_radio {

static const char *const TAG = "wmbus.cc1101";
static constexpr uint32_t F_XTAL = 26000000;
static constexpr uint8_t RXFIFO = 0x3F, RXBYTES = 0x3B, RSSI = 0x34;
static constexpr uint8_t SRES = 0x30, SCAL = 0x33, SRX = 0x34, SIDLE = 0x36, SFRX = 0x3A;

uint8_t CC1101::strobe(uint8_t cmd) {
  this->enable();
  uint8_t result = this->transfer_byte(cmd);
  this->disable();
  return result;
}
uint8_t CC1101::read_reg(uint8_t addr, bool status) {
  this->enable();
  this->transfer_byte(addr | (status ? 0xC0 : 0x80));
  uint8_t result = this->transfer_byte(0);
  this->disable();
  return result;
}
void CC1101::write_reg(uint8_t addr, uint8_t value) {
  this->enable();
  this->transfer_byte(addr);
  this->transfer_byte(value);
  this->disable();
}
void CC1101::read_fifo(uint8_t *buf, size_t count) {
  this->enable();
  this->transfer_byte(RXFIFO | 0xC0);
  this->read_array(buf, count);
  this->disable();
}

void CC1101::setup() {
  this->spi_setup();
  this->gdo2_pin_->setup();
  this->gdo0_pin_->setup();
  this->strobe(SRES);
  delay(10);
  const auto partnum = this->read_reg(0x30, true);
  if (partnum != 0) {
    ESP_LOGE(TAG, "Bad PARTNUM: 0x%02x", partnum);
    this->mark_failed();
    return;
  }
  // Register set adapted from SzczepanLeon CC1101, preserving C/T 100kbps 2-FSK RX.
  static const std::pair<uint8_t, uint8_t> regs[] = {
      {0x00, 0x06},  // IOCFG2: SYNC asserted, active high; rising IRQ on GDO2
      {0x01, 0x2E},  // IOCFG1: high impedance
      {0x02, 0x40},  // IOCFG0: inverted RX FIFO threshold
      {0x03, 0x07},  // FIFOTHR: RX >= 32 bytes
      {0x04, 0x54}, {0x05, 0x3D}, {0x06, 0xFF}, {0x07, 0x00}, {0x08, 0x02}, {0x09, 0x00}, {0x0A, 0x00}, {0x0B, 0x08},
      {0x0C, 0x00}, {0x10, 0x5C}, {0x11, 0x04}, {0x12, 0x06}, {0x13, 0x22}, {0x14, 0xF8}, {0x15, 0x44}, {0x16, 0x07},
      {0x17, 0x00}, {0x18, 0x18}, {0x19, 0x2E}, {0x1A, 0xBF}, {0x1B, 0x43}, {0x1C, 0x09}, {0x1D, 0xB5}, {0x1E, 0x87},
      {0x1F, 0x6B}, {0x20, 0xFB}, {0x21, 0xB6}, {0x22, 0x10}, {0x23, 0xEA}, {0x24, 0x2A}, {0x25, 0x00}, {0x26, 0x1F},
      {0x27, 0x41}, {0x28, 0x00}, {0x29, 0x59}, {0x2A, 0x7F}, {0x2B, 0x3F}, {0x2C, 0x81}, {0x2D, 0x35}, {0x2E, 0x09},
  };
  for (auto [addr, value] : regs)
    this->write_reg(addr, value);
  const uint32_t fr = (uint64_t(this->frequency_hz_) << 16) / F_XTAL;
  this->write_reg(0x0D, byte(fr, 2));
  this->write_reg(0x0E, byte(fr, 1));
  this->write_reg(0x0F, byte(fr, 0));
  this->strobe(SCAL);
  delay(1);
  this->strobe(SFRX);
  this->strobe(SRX);
  ESP_LOGI(TAG, "CC1101 setup at %u Hz", this->frequency_hz_);
}

void CC1101::dump_config() {
  Transceiver::dump_config();
  ESP_LOGCONFIG(TAG, "Frequency: %u Hz", this->frequency_hz_);
  LOG_PIN("GDO2 Sync Pin: ", this->gdo2_pin_);
  LOG_PIN("GDO0 FIFO Threshold Pin: ", this->gdo0_pin_);
}

void CC1101::start_receiver(TaskHandle_t receiver_task) {
  this->data_ready_interrupt_ = {receiver_task, 1};
  // GDO2 active high when sync is detected; works even for <32-byte frames.
  this->gdo2_pin_->attach_interrupt(&Transceiver::notify_interrupt, &this->data_ready_interrupt_,
                                    gpio::INTERRUPT_RISING_EDGE);
}

void CC1101::reset_receiver() {
  this->strobe(SIDLE);
  delay(1);
  this->strobe(SFRX);
  ulTaskNotifyTake(pdTRUE, 0);
  this->awaiting_start_ = true;
  this->rssi_captured_ = false;
  this->strobe(SRX);
}

bool CC1101::read(std::span<uint8_t> buf, TickType_t first_byte_timeout) {
  if (buf.empty())
    return true;

  // A C/T frame may be shorter than the CC1101 FIFO threshold.
  // Start on GDO2 SYNC instead of waiting for the GDO0 threshold interrupt.
  if (this->awaiting_start_) {
    if (!ulTaskNotifyTake(pdTRUE, first_byte_timeout))
      return false;
    this->awaiting_start_ = false;
  }

  TickType_t last_progress = xTaskGetTickCount();
  while (!buf.empty()) {
    const uint8_t raw = this->read_reg(RXBYTES, true);
    if (raw & 0x80) {
      ESP_LOGW(TAG, "CC1101 RX FIFO overflow");
      return false;  // radio task will reset and flush
    }

    const size_t available = raw & 0x7F;
    if (available > 0) {
      if (!this->rssi_captured_) {
        this->last_raw_rssi_ = this->read_reg(RSSI, true);
        this->rssi_captured_ = true;
      }
      size_t count = std::min(buf.size(), available);
      // CC1101 RX FIFO erratum: keep at least one byte buffered while
      // expecting more bytes to avoid an empty FIFO during an active frame.
      if (buf.size() > count && count == available) {
        if (count > 1)
          --count;
        else
          count = 0;
      }
      if (count > 0) {
        this->read_fifo(buf.data(), count);
        buf = buf.subspan(count);
        last_progress = xTaskGetTickCount();
        continue;
      }
    }

    if ((xTaskGetTickCount() - last_progress) > pdMS_TO_TICKS(500)) {
      ESP_LOGW(TAG, "CC1101 RX timeout waiting for remaining %u byte(s)", static_cast<unsigned>(buf.size()));
      return false;
    }
    vTaskDelay(1);
  }
  return true;
}

int8_t CC1101::get_rssi() {
  const int16_t r = this->last_raw_rssi_;
  const int16_t signed_r = (r >= 128 ? r - 256 : r);
  return static_cast<int8_t>(signed_r / 2 - (this->frequency_hz_ < 600000000 ? 76 : 74));
}

}  // namespace wmbus_radio
}  // namespace esphome
