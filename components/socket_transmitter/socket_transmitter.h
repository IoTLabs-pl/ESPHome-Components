#pragma once
#include <span>
#include <string>
#include <vector>

#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"

namespace esphome {
namespace socket_transmitter {
class SocketTransmitter : public Component {
 public:
  SocketTransmitter(const char *host, uint16_t port, int protocol) : host_(host), port_(port), protocol_(protocol) {}
  void send(std::span<const uint8_t> data);
  void send(std::string_view data);
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

 protected:
  const char *host_;
  uint16_t port_;
  int protocol_;
  std::unique_ptr<socket::Socket> socket_;
};
}  // namespace socket_transmitter
}  // namespace esphome
