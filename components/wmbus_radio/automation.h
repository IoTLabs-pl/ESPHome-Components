#pragma once

#include "esphome/core/automation.h"
#include "component.h"
#include "packet.h"

namespace esphome {
namespace wmbus_radio {
class FrameTrigger : public Trigger<Frame *> {
 public:
  explicit FrameTrigger(wmbus_radio::Radio *radio, bool mark_handled) {
    radio->add_frame_handler([this, mark_handled](Frame *frame) {
      this->trigger(frame);
      if (mark_handled)
        frame->mark_as_handled();
    });
  }
};
class PacketTrigger : public Trigger<Packet *> {
 public:
  explicit PacketTrigger(wmbus_radio::Radio *radio) {
    radio->add_packet_handler([this](Packet *packet) {
      this->trigger(packet);
    });
  }
};

}  // namespace wmbus_radio
}  // namespace esphome