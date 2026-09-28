#include "meter.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esphome/components/json/json_util.h"
#include "esphome/core/alloc_helpers.h"

#include "quirks.h"
#include "walk.h"

namespace wmbus {

using namespace std;

bool MatcherSpec::vif_listed(uint16_t vif) const {
  return std::ranges::any_of(vif_ranges, [vif](VifSpan span) { return span.contains(vif); });
}

bool MatcherSpec::combinable_listed(uint16_t raw) const {
  return std::ranges::any_of(combinables, [raw](VifSpan span) { return span.contains(raw); });
}

bool MatcherSpec::matches(const Record &e) const {
  if (!active)
    return false;
  if (dif_vif_key[0] != 0)
    return e.key_equals(dif_vif_key);

  const bool range = vif_ranges.empty() || vif_listed(e.vif);
  const bool type = measurement_type == MeasurementType::Any || e.measurement_type == measurement_type;
  const bool storage_ok = !storage || storage->contains(e.storage_nr);
  const bool tariff_ok = !tariff || tariff->contains(e.tariff_nr);
  const bool subunit_ok = !subunit || subunit->contains(e.subunit_nr);
  if (!(range && type && storage_ok && tariff_ok && subunit_ok))
    return false;

  if (combinables.empty() && !any_combinable)
    return e.combinables.empty();
  if (!std::ranges::all_of(combinables, [&e](VifSpan span) { return e.has_combinable_in(span); }))
    return false;
  return any_combinable || std::ranges::all_of(e.combinables, [this](uint16_t raw) { return combinable_listed(raw); });
}

bool MatcherSpec::matches_loosely(const Record &e) const {
  if (dif_vif_key[0] != 0)
    return e.key_equals(dif_vif_key);
  // Any and Instantaneous both mean "unconstrained" here.
  return vif_listed(e.vif) &&
         (measurement_type == MeasurementType::Any || measurement_type == MeasurementType::Instantaneous ||
          measurement_type == e.measurement_type) &&
         (!storage || storage->from == e.storage_nr) && (!tariff || tariff->from == e.tariff_nr);
}

static double any_factor(std::span<const RangeFactor> factors, uint16_t vif) {
  const auto f = std::ranges::find_if(factors, [vif](const RangeFactor &f) { return f.range.contains(vif); });
  return f == factors.end() ? NOT_A_NUMBER : f->factor;
}

void MeterState::extract_numeric(const FieldSpec &f, const Record &dve) {
  const std::optional<double> extracted =
      dve.extract_double(f.vif_scaling == VifScaling::Auto, f.dif_signedness == DifSignedness::Unsigned);
  if (!extracted)
    return;

  double value = *extracted;
  if (f.date_record) {
    DecodedDate date = dve.extract_date();
    // An unset date (0xffff and its like) is null, not a year 2127.
    value = date.valid ? (double) mktime(&date.value) : NOT_A_NUMBER;
  }
  value *= f.scale;
  if (!f.any_factors.empty())
    value *= any_factor(f.any_factors, dve.vif);
  if (f.null_value && value == *f.null_value)
    value = NOT_A_NUMBER;
  values[f.slot] = value;
}

void MeterState::extract_tpl_status(const FieldSpec &f, const Telegram &t) {
  Status status;
  status.add("OK");
  status.add(spec->tpl_status->translate(t.tpl->sts));
  values[f.slot] = status.str();
}

void MeterState::extract_string(const FieldSpec &f, const Telegram &t, const Record &dve) {
  if (f.lookup == nullptr && f.include_tpl_status) {
    extract_tpl_status(f, t);
    return;
  }
  if (f.lookup != nullptr) {
    const std::optional<uint64_t> bits = dve.extract_long();
    if (!bits)
      return;
    Status status = f.lookup->translate(*bits);
    if (f.include_tpl_status)
      status.add(spec->tpl_status->translate(t.tpl->sts));
    values[f.slot] = status.str();
    return;
  }

  switch (f.text_form) {
    case TextForm::DateTime: {
      const DecodedDate date = dve.extract_date();
      // Six bytes is a date with seconds and a zone.
      const TimeFormat fmt = dve.bytes().size() == 6 ? TimeFormat::DateTimeSec : TimeFormat::DateTime;
      values[f.slot] = date.valid ? format_time(date.value, fmt) : "";
      break;
    }
    case TextForm::Date: {
      const DecodedDate date = dve.extract_date();
      values[f.slot] = date.valid ? format_time(date.value, TimeFormat::Date) : "";
      break;
    }
    case TextForm::Readable:
      values[f.slot] = dve.readable_string(false);
      break;
    case TextForm::Reversed:
      values[f.slot] = dve.readable_string(true);
      break;
    case TextForm::Hex:
      values[f.slot] = dve.hex_string();
      break;
  }
}

void MeterState::extract(const FieldSpec &f, const Telegram &t, const Record &dve) {
  if (f.kind == FieldKind::Text)
    extract_string(f, t, dve);
  else if (f.calculate != nullptr)
    values[f.slot] = f.calculate(*this, &dve);
  else
    extract_numeric(f, dve);
}

std::optional<size_t> DecoderSpec::transform_payload(Key &key, Telegram &t, std::span<uint8_t> content) const {
  const PayloadTransform &pt = *transform;
  if (pt.tpl_acc_offset >= content.size() || pt.offset >= content.size())
    return {};

  size_t payload_end = content.size();
  if (pt.length > 0) {
    payload_end = pt.offset + pt.length;
    if (payload_end > content.size())
      return {};
  }
  if (!key)
    return {};

  t.tpl->acc = content[pt.tpl_acc_offset];

  const std::span<uint8_t> encrypted = content.subspan(pt.offset, payload_end - pt.offset);
  if (!t.aes_cbc_iv_decrypt(encrypted, key))
    return {};

  memmove(content.data(), encrypted.data(), encrypted.size());
  return encrypted.size();
}

void DecoderSpec::note_decoding_error(Telegram &t) const {
  if (required)
    t.decoding_errors.add(string("DECODING_ERROR_") + name);
}

void MeterState::run_decoders(Telegram &t) {
  for (const DecoderSpec &d : spec->decoders) {
    if (d.scope == MatchScope::Frame) {
      if (!t.add_ixml_records(0, t.frame, *d.grammar))
        d.note_decoding_error(t);
      continue;
    }

    if (d.scope == MatchScope::Payload) {
      // Records point into these bytes, so a rewritten payload must live in `derived`.
      std::span<const uint8_t> bytes = t.payload();
      if (spec->payload_quirk == PayloadQuirk::DiehlPrios) {
        bytes = t.derived.front();
      } else if (d.transform) {
        std::vector<uint8_t> rewritten(bytes.begin(), bytes.end());
        const std::optional<size_t> transformed = d.transform_payload(key, t, rewritten);
        if (!transformed)
          continue;
        rewritten.resize(*transformed);
        bytes = t.derived.emplace_back(std::move(rewritten));
      }
      if (!t.add_ixml_records(t.header_size, bytes, *d.grammar))
        d.note_decoding_error(t);
      continue;
    }

    // Records this grammar adds are visited too, as upstream re-reads them.
    for_each_record(t, [&](const Record &dve) {
      if (!d.matcher.matches(dve))
        return;

      std::span<const uint8_t> bytes = dve.bytes();
      // A profile's point dies with the visit; the records must outlive it.
      if (!dve.computed.empty())
        bytes = t.derived.emplace_back(bytes.begin(), bytes.end());
      bool decrypted = false;
      if (spec->payload_quirk == PayloadQuirk::TryQundisDecode) {
        if (std::optional<vector<uint8_t>> plain = quirks::qundis_walk_by_decode(t, key, bytes)) {
          bytes = t.derived.emplace_back(std::move(*plain));
          decrypted = true;
        }
      }

      if (!t.add_ixml_records(dve.offset, bytes, *d.grammar)) {
        d.note_decoding_error(t);
        // Mode 5 has no integrity check: a wrong key decrypts into bytes the grammar rejects.
        if (decrypted)
          t.decoding_errors.add("FAILED_DECODE");
      }
    });
  }
}

static std::optional<Record> find_loosely(const MatcherSpec &m, const Telegram &t) {
  if (!m.active)
    return {};
  int nr = m.index_nr;
  std::optional<Record> found;
  for_each_record(t, [&](const Record &r) {
    if (!found && m.matches_loosely(r) && --nr <= 0)
      found = r;
  });
  return found;
}

namespace {

struct ExtractionPass {
  static constexpr uint16_t NO_OWNER = 0xffff;

  std::vector<int> match_nr;
  std::vector<bool> extracted;
  std::vector<uint16_t> owner;  // by slot: the field that last wrote it

  ExtractionPass(size_t fields, size_t slots) : match_nr(fields, 0), extracted(fields, false), owner(slots, NO_OWNER) {}

  // Upstream is field-major, so of two fields sharing a slot the later one wins.
  bool claim(uint16_t slot, size_t field_index) {
    if (owner[slot] != NO_OWNER && owner[slot] > field_index)
      return false;
    owner[slot] = (uint16_t) field_index;
    return true;
  }
};

}  // namespace

// Record-major where upstream is field-major; claim() keeps the same write winning.
void MeterState::process_field_extractors(Telegram &t) {
  const DriverSpec &driver = *spec;
  const size_t field_count = driver.fields.size();

  ExtractionPass pass(field_count, driver.value_slots);

  for_each_record(t, [&](const Record &dve) {
    for (size_t i = 0; i < field_count; ++i) {
      const FieldSpec &f = driver.fields[i];
      if (!f.matcher.active)
        continue;
      if (!f.matcher.matches(dve))
        continue;
      pass.match_nr[i]++;
      if (f.matcher.index_nr != pass.match_nr[i] && !f.extract_all_matches)
        continue;
      if (!pass.claim(f.slot, i))
        continue;
      extract(f, t, dve);
      pass.extracted[i] = true;
    }
  });

  // Last, so a formula may read the values stored above.
  for (size_t i = 0; i < field_count; ++i) {
    const FieldSpec &f = driver.fields[i];
    if (f.matcher.active && pass.extracted[i])
      continue;
    if (f.kind == FieldKind::Text && f.include_tpl_status) {
      if (const std::optional<Record> loose = find_loosely(f.matcher, t))
        extract(f, t, *loose);
      else
        extract_tpl_status(f, t);
    } else if (!f.matcher.active && f.calculate != nullptr) {
      values[f.slot] = f.calculate(*this, nullptr);
    }
  }
}

string LookupRule::to_string(uint64_t bits) const {
  if (this->pre_shift_right)
    bits >>= this->pre_shift_right;
  switch (type) {
    case MapType::BitToString:
      return bit_to_string(bits);
    case MapType::IndexToString:
      return index_to_string(bits);
    case MapType::DecimalsToString:
      return decimals_to_string(bits);
  }
  return "";
}

string LookupRule::bit_to_string(uint64_t bits) const {
  string s;
  bits = bits & mask;
  for (const LookupMap &m : map) {
    const uint64_t from = m.from & mask;
    if (m.test == TestBit::Set) {
      if ((bits & from) != 0) {
        s += m.to;
        s += " ";
        bits = bits & ~m.from;
      }
    }
    if (m.test == TestBit::NotSet) {
      if ((bits & from) == 0) {
        s += m.to;
        s += " ";
      } else {
        bits = bits & ~m.from;
      }
    }
  }
  if (bits != 0)
    s += esphome::str_sprintf("%s_%X ", name, (unsigned) bits);
  if (s.empty())
    s = default_message;
  return s;
}

string LookupRule::index_to_string(uint64_t bits) const {
  string s;
  bits = bits & mask;
  bool found = false;
  for (const LookupMap &m : map) {
    if (bits == (m.from & mask)) {
      s += m.to;
      s += " ";
      found = true;
    }
  }
  if (!found)
    s += esphome::str_sprintf("%s_%X ", name, (unsigned) bits);
  return s;
}

string LookupRule::decimals_to_string(uint64_t bits) const {
  string s;
  int number = bits % mask;
  if (number == 0)
    s = default_message;
  for (const LookupMap &m : map) {
    const int num = m.from % mask;
    if ((number - num) >= 0) {
      s += m.to;
      s += " ";
      number -= num;
    }
  }
  if (number > 0)
    s += esphome::str_sprintf("%s_%d ", name, number);
  return s;
}

Status LookupSpec::translate(uint64_t bits) const {
  Status status;
  for (const LookupRule &r : rules)
    status.add(r.to_string(bits));
  return status;
}

const FieldSpec *MeterState::find_field(const string &name) const {
  if (spec == nullptr)
    return nullptr;
  const auto f = std::ranges::find(spec->fields, name, &FieldSpec::name);
  return f == spec->fields.end() ? nullptr : &*f;
}

// A filled slot always holds its field's kind, fixed at codegen.
template<typename T> static std::optional<T> value_of(const Value &slot) {
  if (holds_alternative<monostate>(slot))
    return {};
  return get<T>(slot);
}

string MeterState::status_field(const string &stored) const {
  Status status;
  status.add("OK");
  status.add(stored);
  for (const FieldSpec &g : spec->fields) {
    if (g.status_role != StatusRole::Injects)
      continue;
    if (const std::optional<string> inject = value_of<string>(values[g.slot]))
      status.add(*inject);
  }
  // Decoding errors follow the flags unsorted, the way upstream joins them.
  const string flags = status.str();
  if (decoding_errors.empty())
    return flags;
  return flags == "OK" ? decoding_errors.str() : flags + " " + decoding_errors.str();
}

void MeterState::set_text_by_name(const char *field_name, const string &value) {
  const FieldSpec *f = find_field(field_name);
  if (f != nullptr)
    values[f->slot] = value;
}

bool MeterState::decode_diehl_prios(Telegram &t) {
  if (!quirks::diehl_prios_decode(t, key))
    return false;
  if (const std::optional<quirks::SapPriosIdentity> who = quirks::sap_prios_identity(t)) {
    set_text_by_name("prefix", who->prefix);
    set_text_by_name("serial_number", who->serial_number);
    set_text_by_name("manufacture_y", who->manufacture_y);
  }
  return true;
}

// Not in the constructor: an auto meter learns its driver from its first telegram.
void MeterState::bind_driver(const DriverSpec &driver) {
  spec = &driver;
  values.assign(driver.value_slots, Value{});
  formats.registered = driver.compact_formats;
}

MeterState::MeterState(const DriverSpec *driver, uint32_t meter_id, std::optional<std::array<uint8_t, 16>> key)
    : id(meter_id), key(std::move(key)) {
  if (driver != nullptr)
    bind_driver(*driver);
}

// Deepest layer first: behind a radio converter the meter is in the TPL header, the
// converter in the DLL, and some drivers' mvt lines match only one of the two.
static std::vector<DetectSpec> identities_of(const Telegram &t) {
  // Bit 15 of M is reserved, but apator08 sets it (0x8614 for APT).
  constexpr uint16_t MFCT_MASK = 0x7fff;

  const auto identity = [](const Address &a) { return DetectSpec{(uint16_t) (a.mfct & MFCT_MASK), a.version, a.type}; };
  std::vector<DetectSpec> who;
  if (t.tpl && t.tpl->address)
    who.push_back(identity(*t.tpl->address));
  who.push_back(identity(t.dll.address));
  return who;
}

string DetectSpec::as_mvt() const {
  char buf[16];
  snprintf(buf, sizeof(buf), "%c%c%c,%02x,%02x", (char) (((mfct >> 10) & 0x1f) + 64),
           (char) (((mfct >> 5) & 0x1f) + 64), (char) ((mfct & 0x1f) + 64), version, type);
  return buf;
}

Result MeterState::handle_telegram(std::span<const uint8_t> frame, int rssi, time_t at) {
  Telegram t;
  const auto names_us = [this](const auto &layer) { return layer && layer->address && layer->address->id == id; };
  if (!t.parse_envelope(frame) || !(t.dll.address.id == id || names_us(t.ell) || names_us(t.tpl)))
    return Result::NotForThisMeter;

  if (spec == nullptr) {
    const DriverSpec *detected = nullptr;
    string tried;
    for (const DetectSpec &who : identities_of(t)) {
      detected = detect_driver(who.mfct, who.version, who.type);
      if (detected != nullptr)
        break;
      tried += (tried.empty() ? "" : " nor ") + who.as_mvt();
    }

    if (detected == nullptr) {
      last_error = "no compiled driver detects " + tried;
      return Result::NotDecoded;
    }
    bind_driver(*detected);
  }

  if (!t.decode(key, formats, spec->payload_quirk) ||
      (spec->payload_quirk == PayloadQuirk::DiehlPrios && !decode_diehl_prios(t))) {
    last_error = t.decryption_failed ? "decryption failed"
                 : key               ? "payload not understood"
                                     : "encrypted, no key configured";
    if (!t.decoding_errors.empty())
      last_error += " (" + t.decoding_errors.str() + ")";
    return Result::NotDecoded;
  }

  rssi_dbm = rssi;
  media_type = spec->force_media_type != nullptr ? spec->force_media_type : t.media_type();
  run_decoders(t);
  process_field_extractors(t);

  decoding_errors = t.decoding_errors;
  last_error.clear();
  timestamp = at ? at : time(nullptr);
  return Result::Ok;
}

Value MeterState::value(const FieldSpec &f) const {
  const Value &stored = values[f.slot];
  if (f.status_role == StatusRole::Holds) {
    if (const std::optional<string> text = value_of<string>(stored))
      return status_field(*text);
  }
  return stored;
}

Value MeterState::value(const string &name) const {
  const FieldSpec *f = find_field(name);
  return f == nullptr ? Value{} : value(*f);
}

string MeterState::to_json() const {
  JsonDocument doc;
  JsonObject root = doc.to<JsonObject>();
  root["_"] = "telegram";
  root["media"] = media_type;
  if (spec != nullptr)
    root["driver"] = spec->name;
  char id_buf[9];
  snprintf(id_buf, sizeof(id_buf), "%08x", (unsigned) id);
  root["id"] = id_buf;

  if (spec != nullptr) {
    for (const FieldSpec &f : spec->fields) {
      const Value v = value(f);
      if (holds_alternative<monostate>(v))
        continue;
      if (const double *number = get_if<double>(&v); number && f.json_date && !std::isnan(*number))
        root[f.name] = format_time(*number, *f.json_date);
      else if (number)
        root[f.name] = *number;
      else
        root[f.name] = get<string>(v);
    }
  }

  if (timestamp.has_value())
    root["timestamp"] = format_time((double) *timestamp, TimeFormat::TimestampUTC);
  root["rssi_dbm"] = rssi_dbm;

  string out;
  serializeJson(doc, out);
  return out;
}

const DriverSpec *detect_driver(uint16_t mfct, uint8_t version, uint8_t type) {
  const auto detects = [&](const DetectSpec &d) {
    return d.mfct == mfct && (d.version == DETECT_ANY || d.version == version) &&
           (d.type == DETECT_ANY || d.type == type);
  };
  const auto spec = std::ranges::find_if(registered_drivers,
                                         [&](const DriverSpec *s) { return std::ranges::any_of(s->detect, detects); });
  return spec == registered_drivers.end() ? nullptr : *spec;
}

double MeterState::field_value(uint16_t slot) const { return value_of<double>(values[slot]).value_or(NOT_A_NUMBER); }

double counter_value(const Record *dve, DVEntryCounterType counter) {
  if (dve == nullptr)
    return NOT_A_NUMBER;
  switch (counter) {
    case DVEntryCounterType::STORAGE_COUNTER:
      return dve->storage_nr;
    case DVEntryCounterType::TARIFF_COUNTER:
      return dve->tariff_nr;
    case DVEntryCounterType::SUBUNIT_COUNTER:
      return dve->subunit_nr;
  }
  return NOT_A_NUMBER;
}

// A date literal converts as UTC calendar time.
double datetime_literal(int year, int month, int day, int hour, int minute, int second) {
  struct tm t{};
  t.tm_year = year - 1900;
  t.tm_mon = month - 1;
  t.tm_mday = day;
  t.tm_hour = hour;
  t.tm_min = minute;
  t.tm_sec = second;
  t.tm_isdst = 0;
  return (double) mktime(&t);
}

double make_date(double year, double month, double day) {
  int y = (int) llround(year);
  int m = (int) llround(month);
  int d = (int) llround(day);
  if (m <= 0 || d <= 0)
    return NOT_A_NUMBER;

  struct tm t{};
  t.tm_year = y - 1900;
  t.tm_mon = m - 1;
  t.tm_mday = d;
  t.tm_isdst = 0;
  return (double) mktime(&t);
}

}  // namespace wmbus
