#include "wmbus_meter.h"
#include "esphome/core/alloc_helpers.h"
#include "esphome/core/log.h"

#include <cmath>

namespace esphome {
namespace wmbus_meter {
static const char *TAG = "wmbus_meter";

void Meter::set_driver(const wmbus::DriverSpec *spec, uint32_t id, std::optional<std::array<uint8_t, 16>> key) {
  this->state_ = wmbus::MeterState(spec, id, std::move(key));
}

std::string Meter::get_id() const { return str_sprintf("%08x", (unsigned) this->state_.id); }

void Meter::set_radio(wmbus_radio::Radio *radio) {
  this->radio = radio;
  radio->on_frame([this](wmbus_radio::Frame *frame) { return this->handle_frame(frame); });
}

void Meter::dump_config() {
  ESP_LOGCONFIG(TAG, "wM-Bus Meter:");
  ESP_LOGCONFIG(TAG, "  ID: %08x", (unsigned) this->state_.id);
  ESP_LOGCONFIG(TAG, "  Driver: %s", this->state_.spec == nullptr ? "auto" : this->state_.spec->name);
  ESP_LOGCONFIG(TAG, "  Key: %s", this->state_.key ? "***" : "not-encrypted");
}

void Meter::on_telegram(std::function<void()> &&callback) {
  this->on_telegram_callback_manager.add(std::move(callback));
}

void Meter::handle_frame(wmbus_radio::Frame *frame) {
  switch (this->state_.handle_telegram(frame->data(), frame->rssi())) {
    case wmbus::Result::NotForThisMeter:
      return;

    case wmbus::Result::NotDecoded:
      // Ours but unreadable: sensors are not refreshed with stale values.
      frame->mark_as_handled();
      ESP_LOGW(TAG, "Telegram from %08x could not be decoded: %s", (unsigned) this->state_.id,
               this->state_.last_error.c_str());
      return;

    case wmbus::Result::Ok:
      frame->mark_as_handled();
      this->defer([this]() { this->on_telegram_callback_manager(); });
      return;
  }
}

std::string Meter::as_json() {
  if (!this->state_.timestamp)
    return "{}";
  return this->state_.to_json();
}

optional<std::string> Meter::get_string_field(std::string field_name) {
  if (!this->state_.timestamp)
    return {};
  if (field_name == "media")
    return std::string(this->state_.media_type);
  if (field_name == "timestamp")
    return wmbus::format_time((double) *this->state_.timestamp, wmbus::TimeFormat::TimestampUTC);

  const wmbus::Value value = this->state_.value(field_name);
  if (const double *number = std::get_if<double>(&value)) {
    const wmbus::FieldSpec *f = this->state_.find_field(field_name);
    if (f && f->json_date && !std::isnan(*number))
      return wmbus::format_time(*number, *f->json_date);
  }
  if (!std::holds_alternative<std::string>(value))
    return {};
  return std::get<std::string>(value);
}

optional<float> Meter::get_numeric_field(std::string field_name) {
  if (!this->state_.timestamp)
    return {};

  // RSSI and timestamp describe the reception, not the meter, so neither is a field.
  if (field_name == "rssi_dbm")
    return this->state_.rssi_dbm;
  if (field_name == "timestamp")
    return *this->state_.timestamp;

  const wmbus::Value value = this->state_.value(field_name);
  if (!std::holds_alternative<double>(value))
    return {};
  const double number = std::get<double>(value);
  return std::isnan(number) ? optional<float>{} : optional<float>((float) number);
}

}  // namespace wmbus_meter
}  // namespace esphome
