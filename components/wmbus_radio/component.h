#pragma once

#include "esphome/core/component.h"
#include "esphome/core/gpio.h"
#include "esphome/core/lock_free_queue.h"
#include "esphome/core/static_task.h"

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
  template<typename F> void on_frame(F &&callback) { this->on_frame_callback_manager_.add(std::forward<F>(callback)); }
  template<typename F> void on_packet(F &&callback) {
    this->on_packet_callback_manager_.add(std::forward<F>(callback));
  }

 protected:
  static void receiver_task(Radio *arg);

  Transceiver *transceiver_{nullptr};
  LockFreeQueue<Packet, 4> packet_queue_;
  StaticTask receiver_task_;

  CallbackManager<void(Frame *)> on_frame_callback_manager_;
  LazyCallbackManager<void(Packet *)> on_packet_callback_manager_;
};
}  // namespace wmbus_radio
}  // namespace esphome
