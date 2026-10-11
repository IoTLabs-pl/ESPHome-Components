#pragma once

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace wmbus {

enum class MeasurementType : uint8_t { Any, Instantaneous, Minimum, Maximum, AtError };

// Codegen resolves upstream's named VIF ranges to these.
struct VifSpan {
  uint16_t from;
  uint16_t to;

  constexpr bool contains(uint16_t v) const { return v >= from && v <= to; }
};

// Filled in as far as it parsed; callers use the partial date too.
struct DecodedDate {
  struct tm value;
  bool valid;
};

struct RecordHeader {
  uint8_t dif;
  uint16_t vif;  // the full vif: 0x13, or 0xFD17
  MeasurementType measurement_type;
  int32_t storage_nr;
  int32_t tariff_nr;
  int32_t subunit_nr;
  std::vector<uint16_t> combinables;
  uint8_t length;  // of the header alone
};

std::optional<RecordHeader> parse_record_header(std::span<const uint8_t> bytes);

struct Record {
  // `computed` holds a compact profile's bytes, since a Record leaves the walk by value.
  std::span<const uint8_t> slice;
  std::vector<uint8_t> computed;
  uint16_t offset;
  uint8_t dif;
  uint16_t vif;  // the full vif: 0x13, or 0xFD17
  MeasurementType measurement_type;
  int32_t storage_nr;
  int32_t tariff_nr;
  int32_t subunit_nr;
  std::vector<uint8_t> id;  // the dif/dife/vif/vife bytes the key is made of
  std::vector<uint16_t> combinables;

  std::span<const uint8_t> bytes() const;

  // Against a key in upstream's notation: "0C13", "8440FF22".
  bool key_equals(const char *key) const;
  bool same_id(std::span<const uint8_t> other) const;

  void push_combinable(uint16_t raw);
  bool has_combinable(uint16_t raw) const;
  bool has_combinable_in(VifSpan span) const;

  std::optional<double> extract_double(bool auto_scale, bool force_unsigned) const;
  std::optional<uint64_t> extract_long() const;
  DecodedDate extract_date() const;
  std::string readable_string(bool reversed) const;
  std::string hex_string() const;

  void set_header(std::span<const uint8_t> key, const RecordHeader &header);
  // dif with the storage nr, dife..., vif, and the Synthetic combinable.
  void set_synthetic_id(uint8_t dif_nibble, int storage_nr, uint8_t vif);
};

MeasurementType dif_measurement_type(uint8_t dif);
int dif_len_bytes(uint8_t dif);
bool dif_is_binary(uint8_t type);
bool dif_is_bcd(uint8_t type);
// An all-FF field is "no value", in a record and in a profile's slot alike.
inline bool all_ff(std::span<const uint8_t> data) {
  return std::ranges::all_of(data, [](uint8_t b) { return b == 0xff; });
}
double vif_scale(uint16_t vif);

// Formatted according to TimeFormat, all printed in UTC (as-is from meter).
enum class TimeFormat { Date, DateTime, DateTimeSec, TimestampUTC };

std::string format_time(const struct tm &t, TimeFormat f);
std::string format_time(double epoch_seconds, TimeFormat f);
void add_months(struct tm &date, int months);
double add_months(double t, int months);

}  // namespace wmbus
