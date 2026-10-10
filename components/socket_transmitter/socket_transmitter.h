#pragma once
#include <span>
#include <string>
#include <vector>

#include "esphome/core/automation.h"
#include "esphome/core/log.h"
#include "esphome/core/component.h"
#include "esphome/components/socket/socket.h"

namespace esphome {
namespace socket_transmitter {
static const char *TAG = "socket_transmitter";

class SocketTransmitter : public Component {
 public:
  void set_host(std::string host) { this->host = std::move(host); }
  void set_port(int port) { this->port = port; }
  void set_protocol(int protocol) { this->protocol = protocol; }
  void send(std::span<const uint8_t> data);
  void send(std::string_view data);
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_CONNECTION; }

 protected:
  std::string host;
  int port;
  int protocol;
  std::unique_ptr<socket::Socket> socket_;
};

template<typename T, typename... Ts>
class SocketTransmitterSendAction : public Action<Ts...>, public Parented<SocketTransmitter> {
 public:
  SocketTransmitterSendAction(SocketTransmitter *parent = nullptr) : Parented<SocketTransmitter>(parent) {}

  TEMPLATABLE_VALUE(T, data)

  void play(const Ts &...x) override { this->parent_->send(this->data_.value(x...)); }
};
}  // namespace socket_transmitter
}  // namespace esphome
