#pragma once
#include "esphome/core/automation.h"
#include "esphome/core/defines.h"

#ifdef USE_MQTT
#include "esphome/components/mqtt/mqtt_client.h"
#endif

#include "wmbus_meter.h"

namespace esphome {
namespace wmbus_meter {
class TelegramTrigger : public Trigger<Meter *> {
 public:
  explicit TelegramTrigger(Meter *meter) {
    meter->on_telegram([this, meter]() { this->trigger(meter); });
  }
};

#ifdef USE_MQTT
// ESPHome 2026.10 dropped mqtt::MQTTPublishAction, so keep our own copy.
template<typename... Ts> class SendTelegramWithMQTTAction : public Action<Ts...> {
 public:
  explicit SendTelegramWithMQTTAction(mqtt::MQTTClientComponent *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(std::string, topic)
  TEMPLATABLE_VALUE(std::string, payload)
  TEMPLATABLE_VALUE(uint8_t, qos)
  TEMPLATABLE_VALUE(bool, retain)

  void play(const Ts &...x) override {
    this->parent_->publish(this->topic_.value(x...), this->payload_.value(x...), this->qos_.value(x...),
                           this->retain_.value(x...));
  }

 protected:
  mqtt::MQTTClientComponent *parent_;
};
#endif

}  // namespace wmbus_meter

}  // namespace esphome
