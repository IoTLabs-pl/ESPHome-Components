#include "wmbus_meter.h"

namespace esphome {
namespace wmbus_meter {
static const char *TAG = "wmbus_meter";

void Meter::set_meter_params(std::string id, std::string driver, std::string key) {
  MeterInfo meter_info;
  meter_info.parse(driver + '-' + id, driver, id + ',', key);

  this->meter = createMeter(&meter_info);
}

std::string Meter::get_id() const {
  if (!this->meter)
    return "";
  return this->meter->addressExpressions()[0].id;
}
MeterType Meter::get_type() const {
  if (!this->meter)
    return MeterType::UnknownMeter;
  return this->meter->driverInfo()->type();
}
std::string Meter::get_driver_name() const {
  if (!this->meter)
    return "";
  return this->meter->driverName().str();
}
bool Meter::is_encrypted() const { return this->meter && this->meter->meterKeys()->hasConfidentialityKey(); }

void Meter::set_radio(wmbus_radio::Radio *radio) {
  this->radio = radio;
  radio->add_frame_handler([this](wmbus_radio::Frame *frame) { return this->handle_frame(frame); });
}

void Meter::dump_config() {
  ESP_LOGCONFIG(TAG, "wM-Bus Meter:");
  ESP_LOGCONFIG(TAG, "  ID: %s", this->get_id().c_str());
  ESP_LOGCONFIG(TAG, "  Driver: %s", this->get_driver_name().c_str());
  ESP_LOGCONFIG(TAG, "  Key: %s", this->is_encrypted() ? "***" : "not-encrypted");
}

void Meter::handle_frame(wmbus_radio::Frame *frame) {
  auto about = AboutTelegram(App.get_friendly_name(), frame->rssi(), FrameType::WMBUS);

  std::vector<Address> adresses;
  bool id_match = false;
  auto telegram = std::make_unique<Telegram>();

  this->meter->handleTelegram(about, frame->data(), false, &adresses, &id_match, telegram.get());

  if (id_match) {
    this->last_telegram = std::move(telegram);
    this->defer([this]() {
      this->on_telegram_callback_manager();
      this->last_telegram = nullptr;
    });

    frame->mark_as_handled();
  }
}

std::string Meter::as_json(bool pretty_print) {
  std::string json;
  this->meter->printMeter(this->last_telegram.get(), nullptr, nullptr, '\t', &json, nullptr, nullptr, nullptr,
                          pretty_print);
  return json;
}

optional<std::string> Meter::get_string_field(std::string field_name) {
  auto field_info = this->meter->findFieldInfo(field_name, Quantity::Text);
  if (field_info)
    return this->meter->getStringValue(field_info);

  return {};
}

optional<float> Meter::get_numeric_field(std::string field_name) {
  // RSSI is not handled by meter but by telegram :/
  if (field_name == "rssi_dbm")
    return this->last_telegram->about.rssi_dbm;

  if (field_name == "timestamp")
    return this->meter->timestampLastUpdate();

  std::string name;
  Unit unit;
  extractUnit(field_name, &name, &unit);

  auto value = this->meter->getNumericValue(name, unit);

  if (!std::isnan(value))
    return value;

  return {};
}

void Meter::on_telegram(std::function<void()> &&callback) {
  this->on_telegram_callback_manager.add(std::move(callback));
}

}  // namespace wmbus_meter
}  // namespace esphome
