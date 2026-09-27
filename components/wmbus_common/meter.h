#pragma once

#include <cstdint>
#include <ctime>
#include <limits>
#include <span>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "records.h"
#include "telegram.h"
#include "wmbusmeters_generated/registry.h"

namespace wmbus {

enum class TestBit : uint8_t { Set, NotSet };

enum class MapType : uint8_t { BitToString, IndexToString, DecimalsToString };

enum class FieldKind : uint8_t { Numeric, Text };

// Factor from one unit of an Any* range to the field's unit.
struct RangeFactor {
  VifSpan range;
  double factor;
};

enum class TextForm : uint8_t { Hex, Readable, Reversed, Date, DateTime };

// JSON prints it as null.
inline constexpr double NOT_A_NUMBER = std::numeric_limits<double>::quiet_NaN();

// monostate: not filled yet.
using Value = std::variant<std::monostate, double, std::string>;
enum class VifScaling : uint8_t { Auto, None };
enum class DifSignedness : uint8_t { Signed, Unsigned };

// What an ixml grammar is matched against.
enum class MatchScope : uint8_t { Entry, Payload, Frame };

enum class DVEntryCounterType : uint8_t { STORAGE_COUNTER, TARIFF_COUNTER, SUBUNIT_COUNTER };

enum class StatusRole : uint8_t {
  None,
  Holds,    // assembled with "OK", the injected texts and the decoding errors
  Injects,  // folded into the Holds field
};

struct CounterRange {
  int16_t from;
  int16_t to;

  constexpr bool contains(int32_t nr) const { return nr >= from && nr <= to; }
};

using MaybeRange = std::optional<CounterRange>;

// "Unconstrained" lives in the value: Any, an empty list, an empty range.
struct MatcherSpec {
  const char *dif_vif_key;  // "" when not matching on a key
  // Required; without any_combinable also the only ones allowed.
  std::span<const VifSpan> combinables;
  bool any_combinable;
  MeasurementType measurement_type;
  std::span<const VifSpan> vif_ranges;
  MaybeRange storage;
  MaybeRange tariff;
  MaybeRange subunit;  // always set: upstream has no AnySubunitNr
  uint8_t index_nr;
  bool active;

  bool matches(const Record &e) const;
  // A status field's fallback: ignores combinables and subunit, as upstream does.
  bool matches_loosely(const Record &e) const;

 private:
  bool vif_listed(uint16_t vif) const;
  bool combinable_listed(uint16_t raw) const;
};

struct LookupMap {
  uint64_t from;
  const char *to;
  TestBit test;
};

struct LookupRule {
  const char *name;
  MapType type;
  uint64_t mask;  // never 0: codegen fills in the bits of the map
  const char *default_message;
  std::span<const LookupMap> map;
  uint8_t pre_shift_right{0};

  std::string to_string(uint64_t bits) const;

 private:
  std::string bit_to_string(uint64_t bits) const;
  std::string index_to_string(uint64_t bits) const;
  std::string decimals_to_string(uint64_t bits) const;
};

struct LookupSpec {
  std::span<const LookupRule> rules;

  Status translate(uint64_t bits) const;
};

struct MeterState;

enum class Result : uint8_t {
  NotForThisMeter,
  NotDecoded,  // see last_error
  Ok,
};

// A `calculate` formula compiled to C++; `dve` is null for a field without a matcher.
typedef double (*CalculateFn)(const MeterState &state, const Record *dve);

// transform_payload = tpl_aes_cbc_iv,offset,length,tpl_acc_offset
struct PayloadTransform {
  uint16_t offset;
  uint16_t length;
  uint16_t tpl_acc_offset;
};

struct FieldSpec {
  const char *name;  // unit suffix included: "total_m3"
  FieldKind kind;
  VifScaling vif_scaling;
  DifSignedness dif_signedness;
  StatusRole status_role;
  bool include_tpl_status;
  double scale;  // force_scale times the matcher-unit-to-field-unit factor
  std::span<const RangeFactor> any_factors;
  std::optional<double> null_value;
  MatcherSpec matcher;
  bool extract_all_matches;  // index_nr does not apply
  const LookupSpec *lookup;
  bool date_record;  // numeric, in seconds since the epoch
  std::optional<TimeFormat> json_date;
  TextForm text_form;
  CalculateFn calculate;
  uint16_t slot;  // index into MeterState::values
};

// An XMQ field whose ixml grammar derives records for the fields to match.
struct DecoderSpec {
  const char *name;
  const IxmlGrammar *grammar;
  MatchScope scope;
  MatcherSpec matcher;                        // Entry: the records the grammar runs over
  std::optional<PayloadTransform> transform;  // Payload: a slice decrypted first
  bool required;                              // a payload the grammar turns down is a decoding error

  // Decrypts a slice of the payload in place; returns the new content length.
  std::optional<size_t> transform_payload(Key &key, Telegram &t, std::span<uint8_t> content) const;
  void note_decoding_error(Telegram &t) const;
};

// XMQ's `*` in an mvt's version or type.
constexpr uint8_t DETECT_ANY = 0xff;

// One `detect { mvt = APA,05,06 }` line; also the identity read out of a frame.
struct DetectSpec {
  uint16_t mfct;
  uint8_t version;
  uint8_t type;

  std::string as_mvt() const;
};

struct DriverSpec {
  const char *name;
  std::span<const DetectSpec> detect;
  std::span<const FieldSpec> fields;
  std::span<const DecoderSpec> decoders;
  uint16_t value_slots;
  std::span<const std::span<const uint8_t>> compact_formats;
  const LookupSpec *tpl_status;  // null when no field includes it
  std::optional<PayloadQuirk> payload_quirk;
  const char *force_media_type;  // null: the telegram names the media
};

struct MeterState {
  const DriverSpec *spec = nullptr;
  uint32_t id = 0;
  Key key;

  // By slot; JSON leaves monostate out and prints NaN as null.
  std::vector<Value> values;

  int rssi_dbm = 0;
  // Of the last decoded telegram; empty until one is.
  std::optional<time_t> timestamp;
  const char *media_type = nullptr;
  Status decoding_errors;
  std::string last_error;

  // Outlives the telegram: a compact frame refers to a layout learned earlier.
  CompactFormats formats;

  // A null driver is `type: auto`: detected from the first telegram for this id.
  MeterState() = default;
  MeterState(const DriverSpec *driver, uint32_t meter_id, std::optional<std::array<uint8_t, 16>> key);

  // The frame comes in without its link-layer CRCs.
  Result handle_telegram(std::span<const uint8_t> frame, int rssi = 0, time_t at = 0);

  // {"media": "water", "total_m3": 12.5, "status": "OK"}; dates as upstream writes them.
  std::string to_json() const;

  // Monostate when absent or unfilled; a STATUS field comes out assembled from the others.
  Value value(const std::string &name) const;

  // NaN when the slot has none.
  double field_value(uint16_t slot) const;

  const FieldSpec *find_field(const std::string &name) const;

 private:
  void bind_driver(const DriverSpec &driver);

  Value value(const FieldSpec &f) const;
  std::string status_field(const std::string &stored) const;
  void set_text_by_name(const char *field_name, const std::string &value);

  void extract(const FieldSpec &f, const Telegram &t, const Record &dve);
  void extract_numeric(const FieldSpec &f, const Record &dve);
  void extract_string(const FieldSpec &f, const Telegram &t, const Record &dve);
  void extract_tpl_status(const FieldSpec &f, const Telegram &t);

  void run_decoders(Telegram &t);
  void process_field_extractors(Telegram &t);

  bool decode_diehl_prios(Telegram &t);
};

// An ambiguous mvt goes to the first match.
const DriverSpec *detect_driver(uint16_t mfct, uint8_t version, uint8_t type);

// Formula values are all doubles: an integer operator rounds, applies itself and converts back.
double counter_value(const Record *dve, DVEntryCounterType counter);
double datetime_literal(int year, int month, int day, int hour, int minute, int second);
double make_date(double year, double month, double day);

inline uint64_t as_integer(double v) { return (uint64_t) __builtin_llround(v); }

inline double modulo(double l, double r) { return r == 0.0 ? __builtin_nan("") : __builtin_fmod(l, r); }

inline double shift_left(double l, double r) {
  return (r < 0.0 || r > 63.0) ? __builtin_nan("") : (double) (as_integer(l) << as_integer(r));
}

inline double shift_right(double l, double r) {
  return (r < 0.0 || r > 63.0) ? __builtin_nan("") : (double) (as_integer(l) >> as_integer(r));
}

inline double bit_and(double l, double r) { return (double) (as_integer(l) & as_integer(r)); }
inline double bit_or(double l, double r) { return (double) (as_integer(l) | as_integer(r)); }
inline double bit_xor(double l, double r) { return (double) (as_integer(l) ^ as_integer(r)); }

}  // namespace wmbus
