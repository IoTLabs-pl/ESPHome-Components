#include "walk.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace wmbus {

std::vector<Record> parse_records(std::span<const uint8_t> frame, std::span<const uint8_t> data,
                                  std::optional<std::span<const uint8_t>> format) {
  // A full frame carries the dif/vif bytes in its data, so taking from one takes from the other.
  const bool shared = !format.has_value();
  std::span<const uint8_t> layout = shared ? data : *format;
  const auto take_layout = [&](size_t n) {
    layout = layout.subspan(n);
    if (shared)
      data = layout;
  };
  const auto take_data = [&](size_t n) {
    data = data.subspan(n);
    if (shared)
      layout = data;
  };
  const auto offset = [&]() { return (uint16_t) (data.data() - frame.data()); };

  std::vector<Record> records;
  while (!layout.empty()) {
    const uint8_t dif = layout[0];
    int datalen = dif_len_bytes(dif);

    if (datalen == -2) {
      if (dif != 0x0f)
        break;
      // Manufacturer data to the end of the frame, as a "0F" record.
      Record mfct{};
      mfct.id.push_back(0x0F);
      mfct.dif = 0x0f;
      mfct.vif = 0x7f;
      mfct.measurement_type = MeasurementType::Instantaneous;
      mfct.offset = offset();
      mfct.slice = data.empty() ? std::span<const uint8_t>{} : data.subspan(1);
      records.push_back(std::move(mfct));
      break;
    }

    if (dif == 0x2f) {
      take_layout(1);
      continue;
    }

    const std::optional<RecordHeader> header = parse_record_header(layout);
    if (!header)
      break;
    Record record{};
    record.set_header(layout, *header);
    take_layout(header->length);

    if (data.empty())
      break;
    if (datalen == -1) {
      datalen = data[0];
      take_data(1);
    }
    // Upstream leaves the last byte out of a record the frame cut short.
    if ((int) data.size() < datalen)
      datalen = std::max(0, (int) data.size() - 1);

    record.slice = data.first(datalen);
    record.offset = offset();
    take_data(datalen);
    records.push_back(std::move(record));
    if (data.empty())
      break;
  }
  return records;
}

enum class ProfileMode : uint8_t { Absolute, Increments, Decrements, SignedDifference };

enum class ProfileDistance : uint8_t {
  NotSpacedInTime,
  Seconds,
  Minutes,
  Hours,
  Days,
  HalfMonth,
  OneMonth,
  ThreeMonths,
  SixMonths
};

struct ProfileHeader {
  uint8_t slot_dif_nibble;
  ProfileMode mode;
  ProfileDistance distance;
  bool binary_unsigned;
  int spacing_step;
  int array_column;
};

// The sibling date record a profile counts its slots from.
struct ProfileBaseDate {
  uint8_t dif_nibble;
  uint8_t vif;
  MeasurementType measurement_type;
  struct tm date;
  int len;
};

// The value a profile's increments count from, in its own coding: an 8 digit BCD
// base carries 4 digit BCD increments (OMS Vol.2 Annex G, Table G.4).
struct ProfileBaseValue {
  uint8_t dif_nibble;
  int len;
  int64_t value;
};

// TAF7 sends no base date: the base value's actuality duration, one spacing further
// per point, places the points in time instead (OMS Vol.2 Annex R, R.3.2).
struct ProfileAge {
  uint32_t base_seconds;
  uint32_t spacing_seconds;
};

// The VIFEs that mark a compact profile, and the pair (FF 77) that marks an expanded slot.
constexpr uint16_t INVERSE_COMPACT_PROFILE = 0x13;
constexpr uint16_t COMPACT_PROFILE_WITH_REGISTER = 0x1e;
constexpr uint16_t COMPACT_PROFILE = 0x1f;
constexpr uint16_t SYNTHETIC_COMBINABLE = 0x7f77;
constexpr uint16_t DATE_VIF = 0x6c;
// Actuality duration VIFs; a generated one is seconds, 32 bit binary.
constexpr VifSpan ACTUALITY_DURATION{0x74, 0x77};
constexpr uint8_t ACTUALITY_DURATION_VIF = 0x74;
constexpr uint8_t ACTUALITY_DURATION_DIF_NIBBLE = 0x04;

static std::optional<ProfileHeader> decode_profile_header(uint8_t spacing_control, uint8_t spacing_value) {
  static const ProfileMode MODES[4] = {ProfileMode::Absolute, ProfileMode::Increments, ProfileMode::Decrements,
                                       ProfileMode::SignedDifference};
  static const ProfileDistance UNITS[4] = {ProfileDistance::Seconds, ProfileDistance::Minutes, ProfileDistance::Hours,
                                           ProfileDistance::Days};
  const uint8_t slot_dif_nibble = spacing_control & 0x0f;
  const uint8_t increment_bits = (spacing_control >> 6) & 0x03;
  const uint8_t spacing_unit = (spacing_control >> 4) & 0x03;

  ProfileHeader h{slot_dif_nibble,
                  MODES[increment_bits],
                  ProfileDistance::NotSpacedInTime,
                  dif_is_binary(slot_dif_nibble) && (increment_bits == 1 || increment_bits == 2),
                  0,
                  0};

  if (spacing_value == 0) {
    h.array_column = spacing_unit + 1;
    return h;
  }
  if (spacing_value <= 250) {
    h.distance = UNITS[spacing_unit];
    h.spacing_step = spacing_value;
    return h;
  }
  if (spacing_value == 253 && spacing_unit == 3) {
    h.distance = ProfileDistance::HalfMonth;
    h.spacing_step = 1;
    return h;
  }
  if (spacing_value == 254 && spacing_unit != 0) {
    h.distance = spacing_unit == 1   ? ProfileDistance::SixMonths
                 : spacing_unit == 2 ? ProfileDistance::ThreeMonths
                                     : ProfileDistance::OneMonth;
    h.spacing_step = spacing_unit == 1 ? 6 : spacing_unit == 2 ? 3 : 1;
    return h;
  }
  return {};
}

static std::optional<uint64_t> decode_slot_unsigned(uint8_t nibble, std::span<const uint8_t> bytes) {
  if (dif_is_binary(nibble)) {
    uint64_t v = 0;
    for (size_t i = 0; i < bytes.size(); i++)
      v |= ((uint64_t) bytes[i]) << (8 * i);
    return v;
  }
  if (dif_is_bcd(nibble)) {
    uint64_t value = 0;
    uint64_t mul = 1;
    for (size_t i = 0; i < bytes.size(); i++) {
      int lo = bytes[i] & 0x0f;
      int hi = (bytes[i] >> 4) & 0x0f;
      if (lo > 9 || hi > 9)
        return {};
      value += (uint64_t) lo * mul;
      mul *= 10;
      value += (uint64_t) hi * mul;
      mul *= 10;
    }
    return value;
  }
  return {};
}

static bool encode_slot_unsigned(uint8_t nibble, uint64_t value, std::span<uint8_t> out) {
  if (dif_is_binary(nibble)) {
    for (size_t i = 0; i < out.size(); i++)
      out[i] = (uint8_t) ((value >> (8 * i)) & 0xff);
    return true;
  }
  if (dif_is_bcd(nibble)) {
    for (size_t i = 0; i < out.size(); i++) {
      uint8_t lo = value % 10;
      value /= 10;
      uint8_t hi = value % 10;
      value /= 10;
      out[i] = (uint8_t) ((hi << 4) | lo);
    }
    return value == 0;
  }
  return false;
}

static std::optional<int64_t> decode_slot_signed(uint8_t nibble, std::span<const uint8_t> bytes, bool binary_unsigned) {
  if (dif_is_bcd(nibble)) {
    const std::optional<uint64_t> u = decode_slot_unsigned(nibble, bytes);
    if (!u || *u > (uint64_t) std::numeric_limits<int64_t>::max())
      return {};
    return (int64_t) *u;
  }
  if (!dif_is_binary(nibble))
    return {};

  uint64_t raw = 0;
  for (size_t i = 0; i < bytes.size(); i++)
    raw |= ((uint64_t) bytes[i]) << (8 * i);
  const int bits = (int) bytes.size() * 8;
  if (binary_unsigned) {
    if (raw > (uint64_t) std::numeric_limits<int64_t>::max())
      return {};
    return (int64_t) raw;
  }
  if (bits < 64) {
    uint64_t sign_mask = (uint64_t) 1 << (bits - 1);
    uint64_t value_mask = ((uint64_t) 1 << bits) - 1;
    raw &= value_mask;
    if (raw & sign_mask)
      raw |= ~value_mask;
  }
  return (int64_t) raw;
}

static bool encode_slot_signed(uint8_t nibble, int64_t value, bool binary_unsigned, std::span<uint8_t> out) {
  if (dif_is_bcd(nibble)) {
    if (value < 0)
      return false;
    return encode_slot_unsigned(nibble, (uint64_t) value, out);
  }
  if (!dif_is_binary(nibble))
    return false;

  const int bits = (int) out.size() * 8;
  uint64_t raw = 0;
  if (binary_unsigned) {
    if (value < 0)
      return false;
    if (bits < 64 && (uint64_t) value > (((uint64_t) 1 << bits) - 1))
      return false;
    raw = (uint64_t) value;
  } else {
    int64_t min_v, max_v;
    if (bits == 64) {
      min_v = std::numeric_limits<int64_t>::min();
      max_v = std::numeric_limits<int64_t>::max();
    } else {
      min_v = -((int64_t) 1 << (bits - 1));
      max_v = ((int64_t) 1 << (bits - 1)) - 1;
    }
    if (value < min_v || value > max_v)
      return false;
    raw = (uint64_t) value;
  }
  for (size_t i = 0; i < out.size(); i++)
    out[i] = (uint8_t) ((raw >> (8 * i)) & 0xff);
  return true;
}

static bool is_signed_binary_illegal_value(std::span<const uint8_t> bytes) {
  if (bytes.empty())
    return false;
  return std::all_of(bytes.begin(), bytes.end() - 1, [](uint8_t b) { return b == 0x00; }) && bytes.back() == 0x80;
}

void Record::set_synthetic_id(uint8_t dif_nibble, int storage_nr, uint8_t vif) {
  id.clear();
  int lsb = storage_nr & 1;
  int remaining = storage_nr >> 1;
  uint8_t first = (dif_nibble & 0x0f) | (lsb ? 0x40 : 0x00);
  if (remaining > 0)
    first |= 0x80;
  id.push_back(first);
  while (remaining > 0) {
    uint8_t dife = remaining & 0x0f;
    remaining >>= 4;
    if (remaining > 0)
      dife |= 0x80;
    id.push_back(dife);
  }
  id.push_back(vif);
  id.push_back(SYNTHETIC_COMBINABLE >> 8);
  id.push_back(SYNTHETIC_COMBINABLE & 0xff);
  dif = first;
}

static bool is_profile_marker(uint16_t raw) {
  return raw == COMPACT_PROFILE || raw == COMPACT_PROFILE_WITH_REGISTER || raw == INVERSE_COMPACT_PROFILE;
}

// Profile markers aside; an import and an export profile differ only by BackwardFlow.
static bool same_profile_combinables(const Record &a, const Record &b) {
  const auto covered = [](const Record &from, const Record &to) {
    return std::all_of(from.combinables.begin(), from.combinables.end(),
                       [&](uint16_t c) { return is_profile_marker(c) || to.has_combinable(c); });
  };
  return covered(a, b) && covered(b, a);
}

// So that points of two profiles stay apart for a field matcher.
static void carry_combinables(const Record &entry, Record &out) {
  for (uint16_t raw : entry.combinables) {
    if (!is_profile_marker(raw))
      out.push_combinable(raw);
  }
  out.push_combinable(SYNTHETIC_COMBINABLE);
}

static std::optional<ProfileBaseValue> find_base_value(const Record &entry, std::span<const Record> before) {
  // Prefer the profile's own combinables, but any base value beats none.
  for (const bool require_same_combinables : {true, false}) {
    for (const Record &c : before) {
      // A variable length record is a profile itself, never a base value.
      if ((c.dif & 0x0f) == 0x0d)
        continue;
      if (require_same_combinables && !same_profile_combinables(c, entry))
        continue;
      if (c.storage_nr != entry.storage_nr)
        continue;
      if ((c.vif & 0xff) != (entry.vif & 0xff))
        continue;
      if (c.tariff_nr != entry.tariff_nr)
        continue;
      if (c.subunit_nr != entry.subunit_nr)
        continue;
      const int len = dif_len_bytes(c.dif & 0x0f);
      if (len <= 0)
        continue;
      if (const std::optional<uint64_t> v = c.extract_long())
        return ProfileBaseValue{(uint8_t) (c.dif & 0x0f), len, (int64_t) *v};
    }
  }
  return {};
}

static std::optional<double> find_base_actuality(const Record &entry, std::span<const Record> before) {
  for (const Record &c : before) {
    if (c.storage_nr != entry.storage_nr)
      continue;
    if (c.tariff_nr != entry.tariff_nr)
      continue;
    if (c.subunit_nr != entry.subunit_nr)
      continue;
    if (!ACTUALITY_DURATION.contains(c.vif))
      continue;
    if (c.has_combinable(SYNTHETIC_COMBINABLE))
      continue;
    // Auto scaling brings seconds, minutes, hours and days alike to hours.
    if (const std::optional<double> hours = c.extract_double(true, true))
      return *hours * 3600.0;
  }
  return {};
}

static std::optional<ProfileBaseDate> find_base_date(const Record &entry, std::span<const Record> before) {
  for (const Record &c : before) {
    if (c.storage_nr != entry.storage_nr)
      continue;
    if (c.tariff_nr != entry.tariff_nr)
      continue;
    if (c.subunit_nr != entry.subunit_nr)
      continue;
    if (c.vif != DATE_VIF)
      continue;
    const DecodedDate d = c.extract_date();
    if (!d.valid)
      continue;
    const size_t len = c.bytes().size();
    if (len != 2 && len != 4)
      continue;
    return ProfileBaseDate{(uint8_t) (c.dif & 0x0f), (uint8_t) (c.vif & 0xff), c.measurement_type, d.value, (int) len};
  }
  return {};
}

// Calendar spacings have no fixed length.
static std::optional<uint32_t> spacing_seconds(ProfileDistance distance, int spacing_step) {
  if (spacing_step <= 0)
    return {};
  switch (distance) {
    case ProfileDistance::Seconds:
      return (uint32_t) spacing_step;
    case ProfileDistance::Minutes:
      return (uint32_t) spacing_step * 60;
    case ProfileDistance::Hours:
      return (uint32_t) spacing_step * 60 * 60;
    case ProfileDistance::Days:
      return (uint32_t) spacing_step * 24 * 60 * 60;
    default:
      return {};
  }
}

static bool step_profile_date(struct tm &date, ProfileDistance distance, int spacing_step, bool inverse) {
  const int direction = inverse ? -1 : 1;
  if (distance == ProfileDistance::OneMonth || distance == ProfileDistance::ThreeMonths ||
      distance == ProfileDistance::SixMonths) {
    add_months(date, direction * std::max(spacing_step, 1));
    return true;
  }
  if (distance == ProfileDistance::HalfMonth) {
    // The boundaries are the 1st and the 16th.
    if (inverse) {
      if (date.tm_mday >= 16) {
        date.tm_mday = 1;
      } else {
        add_months(date, -1);
        date.tm_mday = 16;
      }
    } else if (date.tm_mday <= 15) {
      date.tm_mday = 16;
    } else {
      add_months(date, 1);
      date.tm_mday = 1;
    }
    return true;
  }

  int64_t step_seconds = 0;
  if (distance == ProfileDistance::Seconds)
    step_seconds = spacing_step;
  else if (distance == ProfileDistance::Minutes)
    step_seconds = (int64_t) spacing_step * 60;
  else if (distance == ProfileDistance::Hours)
    step_seconds = (int64_t) spacing_step * 60 * 60;
  else if (distance == ProfileDistance::Days)
    step_seconds = (int64_t) spacing_step * 24 * 60 * 60;
  if (step_seconds <= 0)
    return false;

  const time_t ts = mktime(&date);
  if (ts == (time_t) -1)
    return false;
  const time_t shifted = (time_t) ((int64_t) ts + direction * step_seconds);
  return localtime_r(&shifted, &date) != nullptr;
}

static bool encode_date_bytes(const struct tm &date, std::span<uint8_t> out) {
  int year = date.tm_year + 1900;
  int month = date.tm_mon + 1;
  int day = date.tm_mday;
  if (year < 2000 || year > 2127)
    return false;
  if (month < 1 || month > 12)
    return false;
  if (day < 1 || day > 31)
    return false;

  int y = year - 2000;
  uint8_t lo_date = (uint8_t) ((day & 0x1f) | ((y & 0x07) << 5));
  uint8_t hi_date = (uint8_t) ((month & 0x0f) | ((y & 0x78) << 1));

  if (out.size() == 2) {
    out[0] = lo_date;
    out[1] = hi_date;
    return true;
  }
  if (out.size() == 4) {
    if (date.tm_hour < 0 || date.tm_hour > 23)
      return false;
    if (date.tm_min < 0 || date.tm_min > 59)
      return false;
    out[0] = (uint8_t) (date.tm_min & 0x3f);
    out[1] = (uint8_t) (date.tm_hour & 0x1f);
    out[2] = lo_date;
    out[3] = hi_date;
    return true;
  }
  return false;
}

// Each point is followed by a trailer placing it in time: its date, or its age.
static void expand_profile(const Record &entry, std::span<const Record> before,
                           const std::function<void(const Record &)> &visit) {
  const bool with_register = entry.has_combinable(COMPACT_PROFILE_WITH_REGISTER);
  const bool inverse = entry.has_combinable(INVERSE_COMPACT_PROFILE);
  if (!with_register && !inverse && !entry.has_combinable(COMPACT_PROFILE))
    return;

  const std::span<const uint8_t> bytes = entry.bytes();
  if (bytes.size() < 2)
    return;
  const std::optional<ProfileHeader> header = decode_profile_header(bytes[0], bytes[1]);
  if (!header)
    return;
  const ProfileHeader &h = *header;
  const int slot_bytes = dif_len_bytes(h.slot_dif_nibble);
  if (slot_bytes <= 0)
    return;
  const size_t step = (size_t) slot_bytes;
  if ((bytes.size() - 2) % step != 0)
    return;

  std::optional<ProfileBaseValue> base_value = find_base_value(entry, before);
  std::optional<ProfileBaseDate> base = find_base_date(entry, before);
  std::optional<ProfileAge> age;
  if (!base) {
    const std::optional<uint32_t> spacing = spacing_seconds(h.distance, h.spacing_step);
    const std::optional<double> base_seconds = spacing ? find_base_actuality(entry, before) : std::nullopt;
    if (base_seconds)
      age = ProfileAge{(uint32_t) std::llround(*base_seconds), *spacing};
  }

  const bool incremental = h.mode != ProfileMode::Absolute;
  // A reading is written in the base value's coding, not the increment's: 1 byte
  // increments on a 1995 Wh base produce readings that no longer fit in a byte.
  const bool reconstruct = inverse && incremental && base_value.has_value();
  const uint8_t value_dif_nibble = reconstruct ? base_value->dif_nibble : h.slot_dif_nibble;
  int subunit_nr = entry.subunit_nr;
  if (h.distance == ProfileDistance::NotSpacedInTime && h.array_column > 0)
    subunit_nr += h.array_column - 1;

  // Inverse and with-register profiles read their slots forwards, the others backwards.
  const bool forwards = inverse || with_register;
  const size_t slots = (bytes.size() - 2) / step;
  int index = 0;
  for (size_t i = 0; i < slots; ++i) {
    const std::span<const uint8_t> slot = bytes.subspan(2 + (forwards ? i : slots - 1 - i) * step, step);
    if (all_ff(slot)) {
      // In the incremental modes an empty slot ends the profile.
      if (incremental)
        break;
      continue;
    }
    if (incremental && dif_is_binary(h.slot_dif_nibble) && !h.binary_unsigned && is_signed_binary_illegal_value(slot))
      break;

    const int storage_nr = entry.storage_nr + 1 + index;
    Record point{};
    point.set_synthetic_id(value_dif_nibble, storage_nr, (uint8_t) (entry.vif & 0xff));
    point.measurement_type = entry.measurement_type;
    point.vif = entry.vif;
    point.storage_nr = storage_nr;
    point.tariff_nr = entry.tariff_nr;
    point.subunit_nr = subunit_nr;
    point.offset = entry.offset;
    carry_combinables(entry, point);
    point.slice = slot;

    if (reconstruct) {
      const std::optional<int64_t> delta = decode_slot_signed(h.slot_dif_nibble, slot, h.binary_unsigned);
      if (!delta)
        break;
      const int64_t absolute =
          h.mode == ProfileMode::Decrements ? base_value->value + *delta : base_value->value - *delta;

      // No dif data field is longer than this, so the value always fits.
      std::array<uint8_t, 8> encoded;
      const size_t len = (size_t) base_value->len;
      if (!encode_slot_signed(value_dif_nibble, absolute, false, std::span(encoded).first(len)))
        break;
      point.computed.assign(encoded.begin(), encoded.begin() + len);
      base_value->value = absolute;
    }
    visit(point);

    const int trailer_index = index++;
    Record trailer{};
    trailer.storage_nr = storage_nr;
    trailer.tariff_nr = entry.tariff_nr;
    trailer.subunit_nr = subunit_nr;
    trailer.offset = entry.offset;
    if (base) {
      // Tried once per point, whether the next date encodes or not.
      struct tm next_date = base->date;
      std::array<uint8_t, 4> date_bytes;
      if (!step_profile_date(next_date, h.distance, h.spacing_step, inverse) ||
          !encode_date_bytes(next_date, std::span(date_bytes).first((size_t) base->len)))
        continue;
      base->date = next_date;
      trailer.set_synthetic_id(base->dif_nibble, storage_nr, base->vif);
      trailer.measurement_type = base->measurement_type;
      trailer.vif = base->vif;
      trailer.push_combinable(SYNTHETIC_COMBINABLE);
      trailer.computed.assign(date_bytes.begin(), date_bytes.begin() + base->len);
      visit(trailer);
    } else if (age) {
      const uint32_t seconds = age->base_seconds + age->spacing_seconds * (uint32_t) (trailer_index + 1);
      trailer.set_synthetic_id(ACTUALITY_DURATION_DIF_NIBBLE, storage_nr, ACTUALITY_DURATION_VIF);
      trailer.measurement_type = entry.measurement_type;
      trailer.vif = ACTUALITY_DURATION_VIF;
      carry_combinables(entry, trailer);
      trailer.computed = {(uint8_t) (seconds & 0xff), (uint8_t) ((seconds >> 8) & 0xff),
                          (uint8_t) ((seconds >> 16) & 0xff), (uint8_t) ((seconds >> 24) & 0xff)};
      visit(trailer);
    }
  }
}

static Record record_of(const IxmlEntry &entry) {
  Record r{};
  if (const std::optional<RecordHeader> header = parse_record_header(entry.id))
    r.set_header(entry.id, *header);
  r.slice = entry.data;
  r.offset = entry.offset;
  return r;
}

void for_each_record(const Telegram &t, const std::function<void(const Record &)> &visit) {
  // Entries already visited in place of a frame record, so the tail skips them.
  std::vector<bool> taken;
  const auto untaken = [&](size_t i) { return i >= taken.size() || !taken[i]; };

  for (size_t k = 0; k < t.records.size(); ++k) {
    const Record &frame_record = t.records[k];
    taken.resize(t.ixml_entries.size(), false);
    // An ixml entry under the same key takes the frame record's place.
    size_t replacement = 0;
    while (replacement < t.ixml_entries.size() &&
           !(untaken(replacement) && frame_record.same_id(t.ixml_entries[replacement].id)))
      ++replacement;
    if (replacement < t.ixml_entries.size()) {
      taken[replacement] = true;
      visit(record_of(t.ixml_entries[replacement]));
    } else {
      visit(frame_record);
    }
    expand_profile(frame_record, std::span(t.records).first(k + 1), visit);
  }

  for (size_t i = 0; i < t.ixml_entries.size(); ++i) {
    if (untaken(i))
      visit(record_of(t.ixml_entries[i]));
  }
}

}  // namespace wmbus
