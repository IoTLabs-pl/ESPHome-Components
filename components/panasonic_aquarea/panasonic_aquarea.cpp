#include "panasonic_aquarea.h"
#include "protocol.h"

namespace esphome {
namespace panasonic_aquarea {
static const char *TAG = "panasonic_aquarea";
static const char *RESPONSE_TIMEOUT_TAG = "response_timeout";
static const char *UPDATE_ENABLER_TAG = "update_enabler";

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
    this->awaiting_command_data = std::vector<uint8_t>(Protocol::STANDARD_PAYLOAD_LENGTH);
    this->awaiting_command_dirty_flag_ = false;
  }
}

// ============================================================================
// Transaction Management
// ============================================================================

bool Device::start_response_timeout(bool internal) {
  if (this->comm_state_ != CommunicationState::IDLE) {
    ESP_LOGE(TAG, "Attempted to start a transaction while another is active");
    return false;
  }

  this->comm_state_ = internal ? CommunicationState::INTERNAL_TRANSACTION : CommunicationState::EXTERNAL_TRANSACTION;

  ESP_LOGI(TAG, "Starting %s transaction with heatpump", internal ? "internal" : "external");
  this->set_timeout(RESPONSE_TIMEOUT_TAG, 1500, [this]() {
    ESP_LOGW(TAG, "Response timeout occurred, resetting communication state");
    this->comm_state_ = CommunicationState::IDLE;
  });

  return true;
}

void Device::stop_response_timeout() {
  ESP_LOGI(TAG, "Finishing transaction with heatpump");

  this->cancel_timeout(RESPONSE_TIMEOUT_TAG);
  this->comm_state_ = CommunicationState::IDLE;
}

// ============================================================================
// UART Communication - External Controller Proxy
// ============================================================================

void Device::set_external_controller_uart(uart::UARTComponent *controller) { this->external_controller_ = controller; }

void Device::process_heatpump_data() {
  auto available = this->available();
  if (available == 0)
    return;

  uint8_t buf[available];

  // Read data from heatpump
  this->read_array(buf, available);
  this->response_buffer_.push(buf, available);

  // Forward to external controller if connected (proxy mode)
  if (this->external_controller_ && this->comm_state_ == CommunicationState::EXTERNAL_TRANSACTION)
    this->external_controller_->write_array(buf, available);

  // Try to parse complete messages
  this->parse_out_response();
}

void Device::process_external_controller_data() {
  auto available_bytes = this->external_controller_ ? this->external_controller_->available() : 0;

  if (available_bytes == 0)
    return;

  if (this->comm_state_ == CommunicationState::EXTERNAL_TRANSACTION || this->start_response_timeout(false)) {
    auto available = this->external_controller_->available();
    uint8_t buf[available];

    // Forward data from external controller to heatpump
    this->external_controller_->read_array(buf, available);
    this->write_array(buf, available);

    // Disable automatic polling when external controller is active
    if (this->request_counter_ == 0) {
      ESP_LOGD(TAG, "Disabling polling due to external controller activity");
      this->cancel_timeout(UPDATE_ENABLER_TAG);
      this->request_counter_ = UINT32_MAX;
    }
  }
}

// ============================================================================
// Component Lifecycle
// ============================================================================

void Device::setup() {
  // Disable polling initially
  this->stop_poller();

  // Wait 15 seconds before attempting first communication
  // This gives the heatpump time to fully initialize
  this->set_timeout(UPDATE_ENABLER_TAG, 15000, [this]() {
    ESP_LOGD(TAG, "Marking external controller as non-existent");
    this->external_controller_ = nullptr;

    if (this->start_response_timeout(true)) {
      ESP_LOGD(TAG, "Sending initial request to heatpump");
      auto init_msg = Protocol::Serializer::initial_request();
      this->write_array(init_msg);

      // After initial request, wait one update interval then start regular polling
      this->set_timeout(this->get_update_interval(), [this]() {
        ESP_LOGD(TAG, "Starting regular polling");
        this->start_poller();
      });
    }
  });
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

    const static std::vector<uint8_t> requests[] = {
        Protocol::Serializer::polling_message(),
        Protocol::Serializer::polling_extra_message(),
    };

    this->write_array(requests[idx]);

    this->request_counter_ += 1;
  }
}

void Device::dump_config() {
  auto entities_count =
      std::distance(this->standard_response_entities_.cbegin(), this->standard_response_entities_.cend()) +
      std::distance(this->extra_response_entities_.cbegin(), this->extra_response_entities_.cend());

  ESP_LOGCONFIG(TAG, "Panasonic Heatpump Device");
  ESP_LOGCONFIG(TAG, "  External Controller Installed: ", YESNO(this->external_controller_));
  ESP_LOGCONFIG(TAG, "  Number of Dependent Entities: %d", entities_count);
  LOG_UPDATE_INTERVAL(this);
}

float Device::get_setup_priority() const {
  // Ensure this component is set up after UART Bus
  return setup_priority::BUS - 50.0f;
}

void Device::add_entity(ReadableEntity *entity, Protocol::CategoryByte type) {
  switch (type) {
    case Protocol::CategoryByte::STANDARD:
      this->standard_response_entities_.push_front(entity);
      break;
    case Protocol::CategoryByte::EXTRA:
      this->extra_response_entities_.push_front(entity);
      break;
    default:
      ESP_LOGE(TAG, "Attempted to add entity with unknown category type");
      break;
  }
}

// ============================================================================
// Protocol Parsing
// ============================================================================

bool Device::parse_out_response() {
  // Parse response from buffer
  auto handled = false;
  auto response = Protocol::Parser::parse_response(this->response_buffer_);

  if (response.data.empty())
    return handled;

  switch (response.category) {
    case Protocol::CategoryByte::STANDARD:
      this->supports_extra_query_ = true;
      for (auto *entity : this->standard_response_entities_)
        entity->handle_update(response.data);
      handled = true;
      break;
    case Protocol::CategoryByte::EXTRA:
      for (auto *entity : this->extra_response_entities_)
        entity->handle_update(response.data);
      handled = true;
      break;
    case Protocol::CategoryByte::INITIAL_REQUEST:
      handled = true;
      break;
    default:
      ESP_LOGW(TAG, "Received response with unknown category, ignoring");
      break;
  }

  this->stop_response_timeout();
  return handled;
}

}  // namespace panasonic_aquarea
}  // namespace esphome
