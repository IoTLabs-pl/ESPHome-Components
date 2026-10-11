#include "records.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ranges>

#include "esphome/core/alloc_helpers.h"
#include "esphome/core/helpers.h"

namespace wmbus {

// A two-byte VIF opens with one of these.
static bool is_vif_extension(uint8_t vif) { return vif == 0xfb || vif == 0xfd || vif == 0xef || vif == 0xff; }

std::optional<RecordHeader> parse_record_header(std::span<const uint8_t> bytes) {
  RecordHeader h{};
  size_t i = 0;

  auto take = [&]() -> std::optional<uint8_t> {
    if (i >= bytes.size())
      return {};
    return bytes[i++];
  };

  const std::optional<uint8_t> dif = take();
  if (!dif)
    return {};
  h.dif = *dif;
  h.measurement_type = dif_measurement_type(h.dif);
  h.storage_nr = (h.dif & 0x40) >> 6;

  int difenr = 0;
  bool more = (h.dif & 0x80) != 0;
  while (more && difenr < 10) {
    const std::optional<uint8_t> dife = take();
    if (!dife)
      return {};
    h.subunit_nr |= ((*dife & 0x40) >> 6) << difenr;
    h.tariff_nr |= ((*dife & 0x30) >> 4) << (difenr * 2);
    h.storage_nr |= (*dife & 0x0f) << (1 + difenr * 4);
    more = (*dife & 0x80) != 0;
    difenr++;
  }

  const std::optional<uint8_t> vif = take();
  if (!vif)
    return {};
  h.vif = *vif & 0x7f;
  bool extension_vif = is_vif_extension(*vif);
  if (extension_vif)
    h.vif <<= 8;

  // A VIF written as text: a length byte and the characters.
  if (*vif == 0x7c || *vif == 0xfc) {
    const std::optional<uint8_t> len = take();
    if (!len)
      return {};
    for (uint8_t k = 0; k < *len; ++k) {
      if (!take())
        return {};
    }
  }

  int combinable = 0;
  bool combinable_extension = false;
  more = (*vif & 0x80) != 0;
  int vifenr = 0;
  while (more && vifenr < 10) {
    const std::optional<uint8_t> vife = take();
    if (!vife)
      return {};
    more = (*vife & 0x80) != 0;
    vifenr++;

    if (extension_vif) {
      h.vif |= *vife & 0x7f;
      extension_vif = false;
    } else if (combinable_extension) {
      combinable |= *vife & 0x7f;
      combinable_extension = false;
      h.combinables.push_back((uint16_t) combinable);
    } else {
      combinable = *vife & 0x7f;
      if (combinable == 0x7c || combinable == 0x7f) {
        combinable <<= 8;
        combinable_extension = true;
      } else {
        h.combinables.push_back((uint16_t) combinable);
      }
    }
  }

  h.length = (uint8_t) i;
  return h;
}

void Record::set_header(std::span<const uint8_t> key, const RecordHeader &h) {
  id.assign(key.begin(), key.begin() + h.length);
  dif = h.dif;
  vif = h.vif;
  measurement_type = h.measurement_type;
  storage_nr = h.storage_nr;
  tariff_nr = h.tariff_nr;
  subunit_nr = h.subunit_nr;
  combinables.clear();
  for (uint16_t raw : h.combinables)
    push_combinable(raw);
}

std::span<const uint8_t> Record::bytes() const {
  if (!computed.empty())
    return computed;
  return slice;
}

bool Record::key_equals(const char *key) const {
  for (uint8_t byte : id) {
    if (key[0] != esphome::format_hex_pretty_char(byte >> 4) || key[1] != esphome::format_hex_pretty_char(byte & 0x0f))
      return false;
    key += 2;
  }
  return *key == 0;
}

bool Record::has_combinable(uint16_t raw) const { return std::ranges::find(combinables, raw) != combinables.end(); }

bool Record::has_combinable_in(VifSpan span) const {
  return std::ranges::any_of(combinables, [span](uint16_t listed) { return span.contains(listed); });
}

void Record::push_combinable(uint16_t raw) {
  if (has_combinable(raw))
    return;
  combinables.push_back(raw);
}

bool Record::same_id(std::span<const uint8_t> other) const { return std::ranges::equal(id, other); }

// As upstream reads a BCD digit out of hex text: 'A'-'0' is 17, not 10.
static int bcd_digit(int nibble) { return nibble < 10 ? nibble : nibble + 7; }

struct BcdValue {
  uint64_t raw;
  bool negative;
};

// Little-endian BCD; a top nibble of 0xF means negative.
static BcdValue bcd_value(std::span<const uint8_t> data) {
  const size_t len = data.size();
  BcdValue out{0, false};
  int top = data[len - 1] >> 4;
  if (top == 0xF) {
    out.negative = true;
    top = 0;
  }
  for (size_t i = len; i > 0; --i) {
    int hi = (i == len) ? top : (data[i - 1] >> 4);
    int lo = data[i - 1] & 0x0f;
    out.raw = out.raw * 100 + bcd_digit(hi) * 10 + bcd_digit(lo);
  }
  return out;
}

static uint64_t little_endian(std::span<const uint8_t> data) {
  uint64_t raw = 0;
  for (size_t i = 0; i < data.size(); ++i)
    raw |= ((uint64_t) data[i]) << (8 * i);
  return raw;
}

std::optional<double> Record::extract_double(bool auto_scale, bool force_unsigned) const {
  const std::span<const uint8_t> data = bytes();
  const int t = dif & 0xf;
  // Nothing to extract: a selection, a variable length or a special function.
  if (t == 0x0 || t == 0x8 || t == 0xd || t == 0xf)
    return {};

  double draw = 0;
  if (dif_is_binary(t)) {
    if (data.size() != (size_t) dif_len_bytes(t))
      return {};
    const uint64_t raw = little_endian(data);
    const int bits = (int) data.size() * 8;
    const bool negate = !force_unsigned && (raw & ((uint64_t) 1 << (bits - 1))) != 0;
    draw = (double) raw;
    if (negate) {
      const uint64_t mask = bits == 64 ? 0 : (~((uint64_t) 0) << bits);
      draw = (double) ((int64_t) (mask | raw));
    }
  } else if (dif_is_bcd(t)) {
    if (all_ff(data))
      return {};
    if (data.size() != (size_t) dif_len_bytes(t))
      return {};
    const BcdValue bcd = bcd_value(data);
    draw = (double) bcd.raw;
    if (bcd.negative)
      draw = draw * -1;
  } else if (t == 0x5) {
    if (data.size() != 4)
      return {};
    const uint32_t bits = (uint32_t) little_endian(data);
    float f;
    memcpy(&f, &bits, sizeof(f));
    draw = f;
  } else {
    return {};
  }

  const double scale = auto_scale ? vif_scale(vif) : 1.0;
  return draw / scale;
}

std::optional<uint64_t> Record::extract_long() const {
  const std::span<const uint8_t> data = bytes();
  const int t = dif & 0xf;
  if (dif_is_binary(t)) {
    if (data.size() != (size_t) dif_len_bytes(t))
      return {};
    return little_endian(data);
  }
  if (dif_is_bcd(t)) {
    if (all_ff(data))
      return {};
    if (data.size() != (size_t) dif_len_bytes(t))
      return {};
    const BcdValue bcd = bcd_value(data);
    if (bcd.negative)
      return (uint64_t) (((int64_t) bcd.raw) * -1);
    return bcd.raw;
  }
  return {};
}

static bool extract_date_bytes(uint8_t hi, uint8_t lo, struct tm &date) {
  int day = 0x1f & lo;
  int year1 = (0xe0 & lo) >> 5;
  int month = 0x0f & hi;
  int year2 = (0xf0 & hi) >> 1;
  int year = 2000 + year1 + year2;
  date.tm_mday = day;
  date.tm_mon = month - 1;
  date.tm_year = year - 1900;
  return month <= 12;
}

static bool extract_time_bytes(uint8_t hi, uint8_t lo, struct tm &date) {
  int min = 0x3f & lo;
  int hour = 0x1f & hi;
  date.tm_min = min;
  date.tm_hour = hour;
  return min <= 59 && hour <= 23;
}

DecodedDate Record::extract_date() const {
  const std::span<const uint8_t> data = bytes();
  DecodedDate out{};
  out.value.tm_isdst = -1;
  out.valid = true;
  if (data.size() == 2) {
    out.valid &= extract_date_bytes(data[1], data[0], out.value);
  } else if (data.size() == 4) {
    out.valid &= extract_date_bytes(data[3], data[2], out.value);
    out.valid &= extract_time_bytes(data[1], data[0], out.value);
  } else if (data.size() == 6) {
    out.valid &= extract_date_bytes(data[4], data[3], out.value);
    out.valid &= extract_time_bytes(data[2], data[1], out.value);
    out.value.tm_sec = 0x3f & data[0];
  }
  return out;
}

// Leading zero bytes are padding; what follows must all be printable.
static bool is_likely_ascii(std::span<const uint8_t> data) {
  const auto text = std::ranges::find_if(data, [](uint8_t b) { return b != 0; });
  return text != data.end() &&
         std::all_of(text, data.end(), [](uint8_t b) { return (b >= 20 && b <= 126) || b == 0x0C || b == 0x0A; });
}

static std::string safe_string(std::span<const uint8_t> data, bool reversed) {
  std::string s;
  const auto append = [&s](uint8_t ch) {
    if (ch >= 32 && ch < 127 && ch != '<' && ch != '>') {
      s += (char) ch;
    } else {
      s += '<';
      s += esphome::format_hex_pretty_char(ch >> 4);
      s += esphome::format_hex_pretty_char(ch & 0x0f);
      s += '>';
    }
  };
  if (reversed)
    std::ranges::for_each(data | std::views::reverse, append);
  else
    std::ranges::for_each(data, append);
  return s;
}

static std::string reverse_bcd(std::span<const uint8_t> data) {
  std::string s;
  for (uint8_t b : data | std::views::reverse) {
    s += esphome::format_hex_pretty_char(b >> 4);
    s += esphome::format_hex_pretty_char(b & 0x0f);
  }
  return s;
}

std::string Record::readable_string(bool reversed) const {
  const std::span<const uint8_t> data = bytes();
  const int t = dif & 0xf;
  const bool binary_or_varlen = dif_is_binary(t) || t == 0xD;

  if (reversed) {
    if (binary_or_varlen && is_likely_ascii(data))
      return safe_string(data, false);
    return hex_string();
  }
  if (binary_or_varlen) {
    if (is_likely_ascii(data))
      return safe_string(data, true);
    return reverse_bcd(data);
  }
  if (dif_is_bcd(t))
    return reverse_bcd(data);
  return hex_string();
}

std::string Record::hex_string() const {
  const std::span<const uint8_t> data = bytes();
  return esphome::format_hex_pretty(data.data(), data.size(), 0, false);
}

// -1: the length is the first data byte; -2: no data of its own.
int dif_len_bytes(uint8_t dif) {
  static const int8_t LEN[16] = {0, 1, 2, 3, 4, 4, 6, 8, 0, 1, 2, 3, 4, -1, 6, -2};
  // The idle filler 2F is the one F type that stands for a byte.
  if (dif == 0x2f)
    return 1;
  return LEN[dif & 0x0f];
}

bool dif_is_binary(uint8_t type) {
  return type == 0x1 || type == 0x2 || type == 0x3 || type == 0x4 || type == 0x6 || type == 0x7;
}

bool dif_is_bcd(uint8_t type) { return type == 0x9 || type == 0xA || type == 0xB || type == 0xC || type == 0xE; }

MeasurementType dif_measurement_type(uint8_t dif) {
  switch (dif & 0x30) {
    case 0x00:
      return MeasurementType::Instantaneous;
    case 0x10:
      return MeasurementType::Maximum;
    case 0x20:
      return MeasurementType::Minimum;
    default:
      return MeasurementType::AtError;
  }
}

// Not pow10_int(): float is not precise enough down to 1e-12.
static double pow10(int exp) {
  double p = 1.0;
  for (int i = exp < 0 ? -exp : exp; i > 0; --i)
    p *= 10.0;
  return exp < 0 ? 1.0 / p : p;
}

// Seconds, minutes, hours, days.
static const double TIME_SCALE[4] = {3600.0, 60.0, 1.0, 1.0 / 24.0};

// Primary VIFs: the block picks the starting decade, the low bits step down from it.
double vif_scale(uint16_t vif) {
  vif &= 0x7f7f;

  // Energy Wh/J, volume m3, mass kg, power W and J/h, volume flow.
  if (vif <= 0x17)
    return pow10(6 - (vif & 0x07));
  if (vif <= 0x1f)
    return pow10(3 - (vif & 0x07));
  if (vif <= 0x27)
    return TIME_SCALE[vif & 0x03];
  if (vif <= 0x3f)
    return pow10(6 - (vif & 0x07));
  // Volume flow per minute and per hour: sixths of a decade, and J/h.
  if (vif <= 0x47)
    return 6 * pow10(8 - (vif & 0x07));
  if (vif <= 0x4f)
    return 3600 * pow10(9 - (vif & 0x07));
  // Temperatures, pressure, temperature difference: decades of four codes.
  if (vif <= 0x57)
    return pow10(3 - (vif & 0x07));
  if (vif <= 0x6b)
    return pow10(3 - (vif & 0x03));
  // Dates, HCA units and the fabrication number have no scale.
  if (vif <= 0x6e)
    return 1.0;
  if (vif <= 0x6f)
    return -1.0;
  if (vif <= 0x77)
    return TIME_SCALE[vif & 0x03];
  if (vif == 0x7c)
    return 1.0;
  if (vif <= 0x7f)
    return -1.0;

  // FB: energy in MWh and GJ, one or two decimals.
  if (vif == 0x7b00 || vif == 0x7b01 || vif == 0x7b08 || vif == 0x7b09)
    return pow10(-((vif & 0x1) + 2));
  if (vif == 0x7b1a)
    return 10.0;
  if (vif == 0x7b1b)
    return 1.0;

  // FD: access number, durations, voltage, current, firmware date.
  if (vif == 0x7d08 || vif == 0x7d3a || vif == 0x7d61 || vif == 0x7d74)
    return 1.0;
  if ((vif >= 0x7d2c && vif <= 0x7d2f) || (vif >= 0x7d31 && vif <= 0x7d33))
    return TIME_SCALE[vif & 0x03];
  if (vif >= 0x7d40 && vif <= 0x7d4f)
    return pow10(9 - (vif & 0xf));
  if (vif >= 0x7d50 && vif <= 0x7d5f)
    return pow10(12 - (vif & 0xf));
  return -1;
}

static const char *strftime_format(TimeFormat f) {
  switch (f) {
    case TimeFormat::Date:
      return "%Y-%m-%d";
    case TimeFormat::DateTime:
      return "%Y-%m-%d %H:%M";
    case TimeFormat::DateTimeSec:
      return "%Y-%m-%d %H:%M:%S";
    case TimeFormat::TimestampUTC:
      return "%FT%TZ";
  }
  return "";
}

std::string format_time(const struct tm &t, TimeFormat f) {
  char buf[40];
  strftime(buf, sizeof(buf), strftime_format(f), &t);
  return std::string(buf);
}

std::string format_time(double epoch_seconds, TimeFormat f) {
  time_t t = (time_t) epoch_seconds;
  struct tm broken_down;
  gmtime_r(&t, &broken_down);
  return format_time(broken_down, f);
}

static bool is_leap_year(int year) {
  year += 1900;
  if (year % 4 != 0)
    return false;
  if (year % 400 == 0)
    return true;
  if (year % 100 == 0)
    return false;
  return true;
}

static int get_days_in_month(int year, int month) {
  static const int days_in_months[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month < 0 || month >= 12)
    month = 0;
  int days = days_in_months[month];
  if (month == 1 && is_leap_year(year))
    days += 1;
  return days;
}

// The last day of a month stays the last day of the month it lands in.
void add_months(struct tm &date, int months) {
  const bool is_last_day_in_month = date.tm_mday == get_days_in_month(date.tm_year, date.tm_mon);

  int year = date.tm_year + months / 12;
  int month = date.tm_mon + months % 12;
  while (month > 11) {
    year += 1;
    month -= 12;
  }
  while (month < 0) {
    year -= 1;
    month += 12;
  }

  date.tm_year = year;
  date.tm_mon = month;
  date.tm_mday =
      is_last_day_in_month ? get_days_in_month(year, month) : std::min(date.tm_mday, get_days_in_month(year, month));
}

double add_months(double t, int months) {
  time_t ut = (time_t) t;
  struct tm time;
  gmtime_r(&ut, &time);
  add_months(time, months);
  return (double) mktime(&time);
}

}  // namespace wmbus
