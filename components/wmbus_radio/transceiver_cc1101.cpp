#include "transceiver_cc1101.h"

#include <algorithm>
#include <array>

#include "esphome/core/log.h"

namespace esphome {
namespace wmbus_radio {

ESPHOME_LOG_TAG(TAG, "CC1101");
static constexpr uint8_t READ = 0x80;
static constexpr uint8_t BURST = 0x40;

enum class CC1101::Strobe : uint8_t {
  SRES = 0x30,
  SRX = 0x34,
  SIDLE = 0x36,
  SFRX = 0x3A,
};

enum class GdoOutput : uint8_t {
  SYNC_WORD = 0x06,
  HIGH_IMPEDANCE = 0x2E,
};

void CC1101::setup() {
  this->gdo_pin_->setup();
  this->spi_setup();

  ESP_LOGV(TAG, "Setting up CC1101");
  ESP_LOGVV(TAG, "reset");
  this->strobe(Strobe::SRES);
  delay(1);

  ESP_LOGVV(TAG, "checking chip version");
  constexpr std::array known_versions{0x14, 0x04, 0x17, 0x03};
  const uint8_t version = this->read_register(Register::VERSION);
  if (std::ranges::find(known_versions, version) == known_versions.end()) {
    ESP_LOGE(TAG, "Unknown chip version: %02X", version);
    this->mark_failed(LOG_STR("Unknown chip version"));
    return;
  }

  ESP_LOGVV(TAG, "tri-state GDO2, GDO1 and GDO0");
  const auto high_impedance = static_cast<uint8_t>(GdoOutput::HIGH_IMPEDANCE);
  this->write_register(Register::IOCFG2, {high_impedance, high_impedance, high_impedance});

  ESP_LOGVV(TAG, "route sync word detection to the GDO pin");
  this->write_register(this->gdo_config_, {static_cast<uint8_t>(GdoOutput::SYNC_WORD)});

  ESP_LOGVV(TAG, "set sync word");
  this->write_register(Register::SYNC1, {0x54, 0x3D});

  ESP_LOGVV(TAG, "disable status bytes, whitening and crc check, set infinite packet length");
  constexpr uint8_t infinite_packet_length = 0b10;
  this->write_register(Register::PKTCTRL1, {0x00, infinite_packet_length});

  ESP_LOGVV(TAG, "set radio frequency");
  constexpr uint32_t f_xosc = 26000000;
  constexpr uint32_t frequency = 868950000;
  constexpr uint32_t freq = (uint64_t{frequency} << 16) / f_xosc;
  this->write_register(Register::FREQ2, {byte(freq, 2), byte(freq, 1), byte(freq, 0)});

  ESP_LOGVV(TAG, "set 232 kHz bandwidth, 100 kBaud 2-FSK and 16-bit sync word above carrier sense");
  constexpr uint32_t bitrate = 100000;
  constexpr uint8_t drate_e = 11;
  constexpr uint8_t drate_m = (uint64_t{bitrate} << (28 - drate_e)) / f_xosc - 256;
  this->write_register(Register::MDMCFG4, {0x70 | drate_e, drate_m, 0x06});

  ESP_LOGVV(TAG, "set 50.8 kHz frequency deviation");
  this->write_register(Register::DEVIATN, {0x50});

  ESP_LOGVV(TAG, "calibrate when entering RX, set frequency offset compensation, bit synchronization and AGC");
  this->write_register(Register::MCSM0, {0x18, 0x2E, 0xBF, 0x43, 0x09, 0xB5});

  ESP_LOGVV(TAG, "set SmartRF Studio intermediate frequency, front end, synthesizer calibration and test values");
  this->write_register(Register::FSCTRL1, {0x08});
  this->write_register(Register::FREND1, {0xB6});
  this->write_register(Register::FSCAL3, {0xEA, 0x2A, 0x00, 0x1F});
  this->write_register(Register::TEST2, {0x81, 0x35, 0x09});

  ESP_LOGI(TAG, "CC1101 setup done");
}

void CC1101::dump_config() {
  Transceiver::dump_config();
  LOG_PIN(this->gdo_config_ == Register::IOCFG0 ? "GDO0 Pin: " : "GDO2 Pin: ", this->gdo_pin_);
}

void CC1101::start_receiver(TaskHandle_t receiver_task) {
  this->sync_word_interrupt_ = {receiver_task, 1};
  this->gdo_pin_->attach_interrupt(&Transceiver::notify_interrupt, &this->sync_word_interrupt_,
                                   gpio::INTERRUPT_RISING_EDGE);
}

void CC1101::reset_receiver() {
  this->strobe(Strobe::SIDLE);
  this->strobe(Strobe::SFRX);
  ulTaskNotifyTake(pdTRUE, 0);
  this->strobe(Strobe::SRX);
}

bool CC1101::read(std::span<uint8_t> buffer, TickType_t first_byte_timeout) {
  // The GDO pin stays high from the sync word until reset_receiver().
  if (!this->gdo_pin_->digital_read() && !ulTaskNotifyTake(pdTRUE, first_byte_timeout))
    return false;

  while (!buffer.empty()) {
    const size_t bytes = this->read_fifo(buffer);
    if (bytes != 0) {
      buffer = buffer.subspan(bytes);
      continue;
    }

    delay(SINGLE_BYTE_TIMEOUT);
    if (this->available_bytes() == 0)
      return false;
  }
  return true;
}

int8_t CC1101::get_rssi() {
  constexpr int8_t rssi_offset = 74;
  const auto rssi = static_cast<int8_t>(this->read_stable_register(Register::RSSI));
  return rssi / 2 - rssi_offset;
}

const char *CC1101::get_name() { return TAG; }

void CC1101::strobe(Strobe command) {
  this->enable();
  this->write_byte(static_cast<uint8_t>(command));
  this->disable();
}

uint8_t CC1101::read_register(Register address) {
  this->enable();
  this->write_byte(static_cast<uint8_t>(address) | READ | BURST);
  const uint8_t value = this->read_byte();
  this->disable();
  return value;
}

// Errata: registers the radio updates may read corrupted, two equal reads in a row are valid.
uint8_t CC1101::read_stable_register(Register address) {
  uint8_t previous, value = this->read_register(address);
  do {
    previous = value;
    value = this->read_register(address);
  } while (value != previous);
  return value;
}

// Bursts go byte by byte: without gaps between bytes the CC1101 SPI tops out at 6.5 MHz.
void CC1101::write_register(Register address, std::initializer_list<uint8_t> data) {
  this->enable();
  this->write_byte(static_cast<uint8_t>(address) | BURST);
  for (const uint8_t value : data)
    this->write_byte(value);
  this->disable();
}

uint8_t CC1101::available_bytes() {
  constexpr uint8_t rxfifo_overflow = 0x80;
  const uint8_t rx_bytes = this->read_stable_register(Register::RXBYTES);
  if (rx_bytes & rxfifo_overflow) {
    ESP_LOGW(TAG, "RX FIFO overflow");
    return 0;
  }
  // Errata: reading the last byte while the next one arrives duplicates it.
  return rx_bytes > 1 ? rx_bytes - 1 : 0;
}

size_t CC1101::read_fifo(std::span<uint8_t> target) {
  target = target.first(std::min<size_t>(this->available_bytes(), target.size()));
  if (target.empty())
    return 0;

  this->enable();
  this->write_byte(static_cast<uint8_t>(Register::FIFO) | READ | BURST);
  for (uint8_t &value : target)
    value = this->read_byte();
  this->disable();
  return target.size();
}

}  // namespace wmbus_radio
}  // namespace esphome
