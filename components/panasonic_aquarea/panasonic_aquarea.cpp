#include "panasonic_aquarea.h"
#include "protocol.h"
#include "extractor.h"

#include <algorithm>
#include <array>
#include <span>

namespace esphome {
namespace panasonic_aquarea {
ESPHOME_LOG_TAG(TAG, "panasonic_aquarea");
static constexpr uint32_t RESPONSE_TIMEOUT_ID = 0;
static constexpr uint32_t RESUME_POLLING_ID = 1;

static constexpr uint32_t RESPONSE_TIMEOUT_MS = 2000;
static constexpr uint32_t EXTERNAL_CONTROLLER_SILENCE_MS = 60000;  // CZ-TAW1 boots in ~35-40 s
static constexpr size_t UART_CHUNK_SIZE = 64;
static constexpr size_t EXTRA_SUPPORT_BYTE_INDEX = 199;
static constexpr uint8_t EXTRA_SUPPORT_THRESHOLD = 0x03;

// ============================================================================
// Command Queue Management
// ============================================================================

void Device::handle_command_queue() {
  if (!this->awaiting_command_dirty_flag_)
    return;

  if (this->start_response_timeout(true)) {
    ESP_LOGD(TAG, "Sending command message");

    auto msg_bytes = Protocol::Serializer::command_message(this->awaiting_command_data);
    this->write_array(msg_bytes);
    this->awaiting_command_data.fill(0);
    this->awaiting_command_dirty_flag_ = false;
  }
}

// ============================================================================
// Transaction Management
// ============================================================================

bool Device::start_response_timeout(bool internal) {
  if (this->comm_state_ != CommunicationState::IDLE) {
    ESP_LOGV(TAG, "Bus busy, not starting %s transaction",
             internal ? LOG_STR_LITERAL("internal") : LOG_STR_LITERAL("external"));
    return false;
  }

  this->comm_state_ = internal ? CommunicationState::INTERNAL_TRANSACTION : CommunicationState::EXTERNAL_TRANSACTION;

  ESP_LOGD(TAG, "Starting %s transaction with heatpump",
           internal ? LOG_STR_LITERAL("internal") : LOG_STR_LITERAL("external"));
  this->set_timeout(RESPONSE_TIMEOUT_ID, RESPONSE_TIMEOUT_MS, [this]() {
    this->status_set_warning(LOG_STR("Response timeout"));
    this->comm_state_ = CommunicationState::IDLE;
  });

  return true;
}

void Device::stop_response_timeout() {
  ESP_LOGD(TAG, "Finishing transaction with heatpump");

  this->cancel_timeout(RESPONSE_TIMEOUT_ID);
  this->comm_state_ = CommunicationState::IDLE;
}

// ============================================================================
// Polling
// ============================================================================

void Device::yield_to_external_controller() {
  this->stop_poller();
  this->set_timeout(RESUME_POLLING_ID, EXTERNAL_CONTROLLER_SILENCE_MS, [this]() {
    ESP_LOGI(TAG, "External controller silent, resuming polling");
    this->start_poller();
  });
}

// ============================================================================
// UART Communication - External Controller Proxy
// ============================================================================

void Device::set_external_controller_uart(uart::UARTComponent *controller) { this->external_controller_ = controller; }

void Device::process_heatpump_data() {
  this->read_available_chunks<UART_CHUNK_SIZE>([this](const uint8_t *data, size_t len) {
    // Forward to external controller if connected (proxy mode)
    if (this->external_controller_ && this->comm_state_ == CommunicationState::EXTERNAL_TRANSACTION)
      this->external_controller_->write_array(data, len);

    for (const uint8_t *byte = data; byte != data + len; byte++) {
      if (const uint8_t *frame = this->response_reader_.feed(*byte))
        this->handle_response(std::span<const uint8_t>(frame, Protocol::RESPONSE_FRAME_SIZE));
    }
  });
}

void Device::process_external_controller_data() {
  if (this->external_controller_ == nullptr)
    return;

  // During our own transaction controller data waits in its UART RX buffer
  if (this->comm_state_ == CommunicationState::INTERNAL_TRANSACTION)
    return;

  std::array<uint8_t, UART_CHUNK_SIZE> buf;

  while (size_t available = this->external_controller_->available()) {
    if (this->comm_state_ == CommunicationState::IDLE) {
      this->start_response_timeout(false);
      this->yield_to_external_controller();
    }

    auto chunk = std::span(buf).first(std::min(available, buf.size()));

    // Forward data from external controller to heatpump
    this->external_controller_->read_array(chunk.data(), chunk.size());
    this->write_array(chunk.data(), chunk.size());
  }
}

// ============================================================================
// Component Lifecycle
// ============================================================================

void Device::setup() {
  if (this->external_controller_)
    this->yield_to_external_controller();
}

void Device::loop() {
  // Process incoming data from heatpump
  this->process_heatpump_data();

  // Process incoming data from external controller (proxy mode)
  this->process_external_controller_data();

  // Send any queued commands
  this->handle_command_queue();
}

void Device::update() {
  // Skip if we're waiting for a response
  if (this->start_response_timeout(true)) {
    // Determine which type of polling message to send
    // Alternate between standard and extra queries if supported
    auto commands_range = this->supports_extra_query_ ? 2 : 1;
    auto idx = this->request_counter_ % commands_range;

    const static Protocol::RequestFrame requests[] = {
        Protocol::Serializer::polling_message(),
        Protocol::Serializer::polling_extra_message(),
    };

    this->write_array(requests[idx]);

    this->request_counter_ += 1;
  }
}

void Device::dump_config() {
  ESP_LOGCONFIG(TAG, "Panasonic Heatpump Device");
  ESP_LOGCONFIG(TAG, "  External Controller Installed: %s", YESNO(this->external_controller_));
  ESP_LOGCONFIG(TAG, "  Number of Dependent Entities: %zu", this->entities_.size());
  LOG_UPDATE_INTERVAL(this);
}

// ============================================================================
// Response Handling
// ============================================================================

void Device::handle_response(std::span<const uint8_t> frame) {
  const auto category = static_cast<Protocol::CategoryByte>(frame[static_cast<size_t>(Protocol::ByteIndex::CATEGORY)]);
  const bool extra = category == Protocol::CategoryByte::EXTRA;
  if (Protocol::calculate_checksum(frame) != 0 || (!extra && category != Protocol::CategoryByte::STANDARD)) {
    ESP_LOGD(TAG, "Dropping invalid frame");
    this->response_reader_.resync();
    return;
  }

  if (!extra)
    this->supports_extra_query_ = frame[EXTRA_SUPPORT_BYTE_INDEX] >= EXTRA_SUPPORT_THRESHOLD;
  auto entities = extra ? this->entities_.subspan(this->standard_entities_count_)
                        : this->entities_.first(this->standard_entities_count_);
  for (auto *entity : entities)
    entity->handle_update(frame);

  this->status_clear_warning();
  this->stop_response_timeout();
}

}  // namespace panasonic_aquarea
}  // namespace esphome
