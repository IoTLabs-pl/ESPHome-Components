#pragma once

#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <vector>

#include "records.h"
#include "telegram.h"

namespace wmbus {

// A compact frame takes the dif/vif bytes from `format`, a full frame from `data`.
std::vector<Record> parse_records(std::span<const uint8_t> frame, std::span<const uint8_t> data,
                                  std::optional<std::span<const uint8_t>> format);

// In upstream's order: each frame record (or the ixml entry replacing it) and its
// profile's points, then the remaining ixml entries, including ones added during
// the walk. A profile's point lives for its visit only.
void for_each_record(const Telegram &t, const std::function<void(const Record &)> &visit);

}  // namespace wmbus
