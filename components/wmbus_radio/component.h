#pragma once

#include <functional>

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/lock_free_queue.h"

#include "esphome/components/spi/spi.h"

#include "packet.h"
#include "transceiver.h"

namespace esphome {
namespace wmbus_radio {

class Radio : public Component {
 public:
  void setup() override;
  void loop() override;
  void receive_frame();

  void set_transceiver(Transceiver *transceiver) { this->transceiver_ = transceiver; }
  void on_frame(std::function<void(Frame *)> &&callback) { this->on_frame_callback_manager_.add(std::move(callback)); }
  void on_packet(std::function<void(Packet *)> &&callback) {
    this->on_packet_callback_manager_.add(std::move(callback));
  }

 protected:
  static void receiver_task(Radio *arg);

  Transceiver *transceiver_{nullptr};
  LockFreeQueue<Packet, 4> packet_queue_;

  CallbackManager<void(Frame *)> on_frame_callback_manager_;
  LazyCallbackManager<void(Packet *)> on_packet_callback_manager_;
};
}  // namespace wmbus_radio
}  // namespace esphome
