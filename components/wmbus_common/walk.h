#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "records.h"
#include "telegram.h"

namespace wmbus {

struct CompactFrame {
  std::span<const uint8_t> format;
  size_t encrypted_end;
  std::vector<uint8_t> full_frame;
};

// A compact frame takes the dif/vif bytes from its format and rebuilds the full frame its CRC
// covers; a full frame (no `compact`) takes them from `data`.
std::vector<Record> parse_records(std::span<const uint8_t> frame, std::span<const uint8_t> data, CompactFrame *compact);

// In upstream's order: each frame record (or the ixml entry replacing it) and its
// profile's points, then the remaining ixml entries, including ones added during
// the walk. A profile's point lives for its visit only.
void for_each_record(const Telegram &t, const std::function<void(const Record &)> &visit);

}  // namespace wmbus
