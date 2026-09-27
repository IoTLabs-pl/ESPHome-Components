#pragma once

#include "esphome/core/component.h"

namespace esphome {
namespace wmbus_test_runner {

// Decodes the vectors from stdin in setup() and exits.
class TestRunner : public Component {
 public:
  void setup() override;
  float get_setup_priority() const override { return setup_priority::LATE; }
};

}  // namespace wmbus_test_runner
}  // namespace esphome
