#include "protocol.h"
#include "panasonic_aquarea.h"

namespace esphome
{
    namespace panasonic_aquarea
    {
        namespace Protocol
        {
            static const char *const TAG = "panasonic_aquarea.protocol";

            bool Parser::find_and_align_to_preamble(ResponseBuffer &buffer)
            {
                buffer.discard_until(static_cast<uint8_t>(PreambleByte::POLLING));

                return buffer.size() > 0; // Return true if preamble found at beginning
            }

            size_t Parser::validate_frame_header(const ResponseBuffer &buffer)
            {
                // Frame structure: preamble (1) + length (1) + direction+category (2) + payload (N) + checksum (1)
                // Length field encodes (direction+category+payload) = (2 + N)
                constexpr size_t MIN_PACKET_SIZE = static_cast<size_t>(ByteIndex::CATEGORY) + 1 + 1; // 4 header bytes + 1 checksum byte
                // Check if we have enough bytes for the header
                if (buffer.size() < MIN_PACKET_SIZE)
                    return 0; // Incomplete header, need more data

                const uint8_t payload_length = buffer.peek(static_cast<size_t>(ByteIndex::PAYLOAD_LENGTH));

                if (payload_length <= MIN_PACKET_SIZE)
                {
                    ESP_LOGW(TAG, "Invalid payload length: %d", payload_length);
                    return MIN_PACKET_SIZE;
                }

                // Therefore total frame size = 1 + 1 + (2 + N) + 1 = N + 5 = payload_length + 3
                const size_t frame_size = static_cast<size_t>(payload_length) + 3;

                if (buffer.size() < frame_size)
                    return 0; // Incomplete frame, need more data

                return frame_size;
            }

            bool Parser::validate_frame_checksum(const std::vector<uint8_t> &buffer)
            {
                return calculate_checksum(buffer) == 0;
            }

            CategoryByte Parser::validate_response_type(const std::vector<uint8_t> &frame)
            {
                const auto category_byte = static_cast<CategoryByte>(frame[static_cast<size_t>(ByteIndex::CATEGORY)]);

                if (category_byte != CategoryByte::STANDARD &&
                    category_byte != CategoryByte::EXTRA)
                {
                    ESP_LOGD(TAG, "Unknown category type: 0x%02X", category_byte);
                    return CategoryByte::UNKNOWN;
                }

                return category_byte;
            }

            Parser::Response Parser::parse_response(ResponseBuffer &buffer)
            {
                // Step 1: Find and align to frame preamble
                if (!find_and_align_to_preamble(buffer))
                    return {}; // Need more data

                // Step 2: Validate frame header and get frame size
                const size_t frame_size = validate_frame_header(buffer);
                if (frame_size == 0)
                    return {}; // Incomplete or invalid header

                std::vector<uint8_t> result;
                result.reserve(frame_size);
                for (size_t i = 0; i < frame_size; i++)
                    result.push_back(buffer.pop());

                // Step 3: Validate checksum over this frame only
                if (!validate_frame_checksum(result))
                    return {};

                // Step 4: Determine response type
                CategoryByte category = validate_response_type(result);
                return Parser::Response{std::move(result), category};
            }

            // ---- Serializer implementation ----
            std::vector<uint8_t> Serializer::serialize_message(
                PreambleByte preamble,
                ThirdByte direction,
                CategoryByte category,
                size_t payload_length)
            {
                // Total frame: PREAMBLE(1) + LENGTH(1) + DIRECTION(1) + CATEGORY(1) + PAYLOAD(N) + CHECKSUM(1)
                const size_t total_size = 1 + 1 + 2 + payload_length + 1;
                std::vector<uint8_t> frame(total_size, 0);
                // Delegate to rvalue overload that mutates in place
                return serialize_message(preamble, direction, category, frame);
            }

            std::vector<uint8_t> Serializer::serialize_message(
                PreambleByte preamble,
                ThirdByte direction,
                CategoryByte category,
                std::vector<uint8_t> &frame)
            {
                // Expect full, zero-filled frame of proper size.
                // Layout: PREAMBLE(0) LENGTH(1) DIRECTION(2) CATEGORY(3) PAYLOAD(...) CHECKSUM(last)
                const size_t total_size = frame.size();
                const size_t payload_length = total_size - 3; // Exclude PREAMBLE, LENGTH, CHECKSUM

                frame[static_cast<size_t>(ByteIndex::PREAMBLE)] = static_cast<uint8_t>(preamble);
                frame[static_cast<size_t>(ByteIndex::PAYLOAD_LENGTH)] = static_cast<uint8_t>(payload_length);
                frame[static_cast<size_t>(ByteIndex::DIRECTION)] = static_cast<uint8_t>(direction);
                frame[static_cast<size_t>(ByteIndex::CATEGORY)] = static_cast<uint8_t>(category);

                // Compute and write checksum
                frame[total_size - 1] = 0u - calculate_checksum(frame);

                return frame;
            }

            std::vector<uint8_t> Serializer::polling_message()
            {
                // Message format: 0x71, 0x6C, 0x01, 0x10, ... (111 bytes total)
                return serialize_message(
                    PreambleByte::POLLING,
                    ThirdByte::X01,
                    CategoryByte::STANDARD,
                    STANDARD_PAYLOAD_LENGTH);
            }

            std::vector<uint8_t> Serializer::polling_extra_message()
            {
                // Message format: 0x71, 0x6C, 0x01, 0x21, ... (111 bytes total)
                return serialize_message(
                    PreambleByte::POLLING,
                    ThirdByte::X01,
                    CategoryByte::EXTRA,
                    STANDARD_PAYLOAD_LENGTH);
            }

            std::vector<uint8_t> Serializer::initial_request()
            {
                // Message format: 0x31, 0x05, 0x10, 0x01, ... (8 bytes total)
                return serialize_message(
                    PreambleByte::INITIAL,
                    ThirdByte::X10,
                    CategoryByte::INITIAL_REQUEST,
                    8);
            }

            std::vector<uint8_t> Serializer::command_message(std::vector<uint8_t> &command)
            {
                // Message format: 0xF1, 0x6C, 0x01, 0x10, ... (111 bytes total)
                return serialize_message(
                    PreambleByte::COMMAND,
                    ThirdByte::X01,
                    CategoryByte::STANDARD,
                    command);
            }
        }

    } // namespace panasonic_aquarea
} // namespace esphome
