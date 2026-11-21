#pragma once

#include "esphome/core/automation.h"
#include "packet.h"

namespace esphome {
namespace wmbus_radio {
struct MarkHandledForwarder {
  Automation<Frame *> *automation;
  void operator()(Frame *frame) const {
    this->automation->trigger(frame);
    frame->mark_as_handled();
  }
};

}  // namespace wmbus_radio
}  // namespace esphome
