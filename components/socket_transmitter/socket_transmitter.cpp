#include "socket_transmitter.h"

namespace esphome {
namespace socket_transmitter {
void SocketTransmitter::send(std::string data) {
  return this->send(std::span((const uint8_t *) data.c_str(), data.length()));
}

void SocketTransmitter::send(std::span<const uint8_t> data) {
  ESP_LOGD(TAG, "Setting up socket transmitter");
  this->socket_ = socket::socket_ip(this->protocol, 0);
  int enable = 1;
  this->socket_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  ESP_LOGD(TAG, "Connecting...");
  sockaddr destination;
  socket::set_sockaddr(&destination, sizeof(destination), this->host, this->port);
  if (this->socket_->connect(&destination, sizeof(destination)) < 0) {
    ESP_LOGE(TAG, "Failed to connect");
    return;
  }

  ESP_LOGD(TAG, "Sending frame [%zu bytes]", data.size());
  int n_bytes = this->socket_->write(data.data(), data.size());
  if (n_bytes < 0)
    ESP_LOGE(TAG, "Failed to send message");
  this->socket_->close();
}

void SocketTransmitter::dump_config() {
  auto protocol = this->protocol == SOCK_DGRAM ? "UDP" : "TCP";

  ESP_LOGCONFIG(TAG, "Socket Transmitter:");
  ESP_LOGCONFIG(TAG, "  Destination: %s:%d", this->host.c_str(), this->port);
  ESP_LOGCONFIG(TAG, "  Protocol: %s", protocol);
}
}  // namespace socket_transmitter
}  // namespace esphome
