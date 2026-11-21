#include "socket_transmitter.h"

namespace esphome {
namespace socket_transmitter {
ESPHOME_LOG_TAG(TAG, "socket_transmitter");

void SocketTransmitter::send(std::string_view data) {
  this->send(std::span<const uint8_t>(reinterpret_cast<const uint8_t *>(data.data()), data.size()));
}

void SocketTransmitter::send(std::span<const uint8_t> data) {
  ESP_LOGD(TAG, "Setting up socket transmitter");
  this->socket_ = socket::socket_ip(this->protocol_, 0);
  if (!this->status_comm_result(this->socket_ != nullptr, LOG_STR("No free socket")))
    return;
  int enable = 1;
  this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  ESP_LOGD(TAG, "Connecting...");
  sockaddr destination;
  socket::set_sockaddr(&destination, sizeof(destination), this->host_, this->port_);
  if (!this->status_comm_result(this->socket_->connect(&destination, sizeof(destination)) >= 0,
                                LOG_STR("Failed to connect")))
    return;

  ESP_LOGD(TAG, "Sending frame [%zu bytes]", data.size());
  this->status_comm_result(this->socket_->write(data.data(), data.size()) >= 0, LOG_STR("Failed to send message"));
  this->socket_->close();
}

void SocketTransmitter::dump_config() {
  auto protocol = this->protocol_ == SOCK_DGRAM ? "UDP" : "TCP";

  ESP_LOGCONFIG(TAG, "Socket Transmitter:");
  ESP_LOGCONFIG(TAG, "  Destination: %s:%u", this->host_, this->port_);
  ESP_LOGCONFIG(TAG, "  Protocol: %s", protocol);
}
}  // namespace socket_transmitter
}  // namespace esphome
