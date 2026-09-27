#include "telegram.h"

#include <algorithm>
#include <cstring>

#include "crypto.h"
#include "quirks.h"
#include "walk.h"

namespace wmbus {

struct MediaType {
  uint8_t type;
  const char *name;
};

static constexpr MediaType MEDIA_TYPES[] = {
    {0x00, "other"},
    {0x01, "oil"},
    {0x02, "electricity"},
    {0x03, "gas"},
    {0x04, "heat"},
    {0x05, "steam"},
    {0x06, "warm water"},
    {0x07, "water"},
    {0x08, "heat cost allocation"},
    {0x09, "compressed air"},
    {0x0a, "cooling load volume at outlet"},
    {0x0b, "cooling load volume at inlet"},
    {0x0c, "heat volume at inlet"},
    {0x0d, "heat/cooling load"},
    {0x0e, "bus/system component"},
    {0x0f, "unknown"},
    {0x15, "hot water"},
    {0x16, "cold water"},
    {0x17, "hot/cold water"},
    {0x18, "pressure"},
    {0x19, "a/d converter"},
    {0x1a, "smoke detector"},
    {0x1b, "room sensor"},
    {0x1c, "gas detector"},
    {0x20, "breaker"},
    {0x21, "valve"},
    {0x25, "customer unit (display device)"},
    {0x28, "waste water"},
    {0x29, "garbage"},
    {0x36, "radio converter (system side)"},
    {0x37, "radio converter (meter side)"},
};

// Techem numbers its media outside the standard.
static constexpr MediaType TCH_MEDIA_TYPES[] = {
    {0x62, "warm water"}, {0x72, "cold water"}, {0x80, "heat cost allocator"},
    {0xc3, "heat"},       {0x43, "heat"},       {0xf0, "smoke detector"},
};

static const char *media_name(std::span<const MediaType> table, uint8_t type) {
  const auto m = std::ranges::find(table, type, &MediaType::type);
  return m == table.end() ? nullptr : m->name;
}

static const char *media_type_of(uint8_t type, uint16_t mfct) {
  if (const char *name = media_name(MEDIA_TYPES, type))
    return name;
  if (type >= 0x1d && type <= 0x3f)
    return "reserved";
  if (mfct == manufacturer_code('T', 'C', 'H')) {
    if (const char *name = media_name(TCH_MEDIA_TYPES, type))
      return name;
  }
  return "Unknown";
}

const Address &Telegram::sender() const {
  if (tpl && tpl->address)
    return *tpl->address;
  if (ell && ell->address)
    return *ell->address;
  return dll.address;
}

const Address &Telegram::meter_address() const { return tpl && tpl->address ? *tpl->address : dll.address; }

const char *Telegram::media_type() const { return media_type_of(sender().type, sender().mfct); }

static uint32_t id_of(std::span<const uint8_t, 4> bytes) {
  return (uint32_t) bytes[3] << 24 | (uint32_t) bytes[2] << 16 | (uint32_t) bytes[1] << 8 | bytes[0];
}

Address Address::parse(std::span<const uint8_t, 8> b) {
  return {(uint16_t) (b[1] << 8 | b[0]), id_of(b.subspan<2, 4>()), b[6], b[7]};
}

static std::array<uint8_t, 8> bytes_of(const Address &a) {
  return {(uint8_t) a.mfct,       (uint8_t) (a.mfct >> 8), (uint8_t) a.id, (uint8_t) (a.id >> 8),
          (uint8_t) (a.id >> 16), (uint8_t) (a.id >> 24),  a.version,      a.type};
}

enum class CiType : uint8_t { ELL, NWL, AFL, TPL };

struct CiField {
  uint8_t value;
  int8_t length;  // -1: ELL V, not supported
  CiType type;
};

static const CiField CI_FIELDS[] = {
    {0x51, 0, CiType::TPL},  {0x72, 0, CiType::TPL},  {0x73, 0, CiType::TPL},  {0x78, 0, CiType::TPL},
    {0x79, 0, CiType::TPL},  {0x7A, 0, CiType::TPL},  {0x7B, 0, CiType::TPL},  {0x81, 0, CiType::NWL},
    {0x8C, 2, CiType::ELL},  {0x8D, 8, CiType::ELL},  {0x8E, 10, CiType::ELL}, {0x8F, 16, CiType::ELL},
    {0x86, -1, CiType::ELL}, {0x90, 10, CiType::AFL},
};

static bool is_ci_of_type(uint8_t ci, CiType type) {
  return std::ranges::any_of(CI_FIELDS, [&](const CiField &f) { return f.value == ci && f.type == type; });
}

static int ci_length(uint8_t ci) {
  const CiField *f = std::ranges::find(CI_FIELDS, ci, &CiField::value);
  return f == std::end(CI_FIELDS) ? -2 : f->length;
}

static bool is_ci_mfct_specific(uint8_t ci) { return ci >= 0xA0 && ci <= 0xB7; }

// The rest are manufacturer-specific payloads: no records.
static bool carries_records(uint8_t ci) {
  return ci == 0x72 || ci == 0x73 || ci == 0x7A || ci == 0x7B || ci == 0x78 || ci == 0x79;
}

static bool payload_crc_ok(std::span<const uint8_t> frame, size_t at) {
  const uint16_t crc = frame[at + 1] << 8 | frame[at];
  return crc == wmbus::crc16_en13757(frame.subspan(at + 2));
}

std::span<const uint8_t> Telegram::payload() const {
  if (frame.size() < (size_t) header_size + suffix_size)
    return {};
  return std::span(frame).subspan(header_size, frame.size() - header_size - suffix_size);
}

std::optional<std::span<const uint8_t>> CompactFormats::lookup(uint16_t signature) const {
  const auto signed_as = [signature](std::span<const uint8_t> format) {
    return wmbus::crc16_en13757(format) == signature;
  };
  if (const auto f = std::ranges::find_if(registered, signed_as); f != registered.end())
    return *f;
  if (const auto f = std::ranges::find_if(learned, signed_as); f != learned.end())
    return std::span<const uint8_t>(*f);
  return {};
}

void CompactFormats::learn(std::span<const uint8_t> bytes) {
  if (bytes.empty() || lookup(wmbus::crc16_en13757(bytes)))
    return;

  if (learned.size() < LEARNED_FORMATS_KEPT)
    learned.emplace_back();
  learned[learned_next].assign(bytes.begin(), bytes.end());
  learned_next = (uint8_t) ((learned_next + 1) % LEARNED_FORMATS_KEPT);
}

size_t Telegram::left() const { return frame.size() - pos_; }

std::optional<Dll> Dll::parse(std::span<const uint8_t> frame) {
  if (frame.size() < SIZE || frame.size() < frame[0])
    return {};
  return Dll{Address::parse(frame.subspan<2, 8>())};
}

std::optional<Ell> Ell::parse(std::span<const uint8_t> frame, size_t at) {
  const uint8_t ci = frame[at];
  const int len = ci_length(ci);
  size_t pos = at + 1;
  if (len < 0 || frame.size() - pos < (size_t) len)
    return {};

  Ell ell{.cc = frame[pos]};
  pos += 2;  // CC and the access counter

  if (ci == 0x8E || ci == 0x8F) {
    ell.address = Address::parse(frame.subspan(pos).first<8>());
    pos += 8;
  }
  if (ci == 0x8D || ci == 0x8F) {
    std::copy_n(frame.begin() + pos, ell.session_number.size(), ell.session_number.begin());
    pos += 4;
    ell.encrypted = ((id_of(ell.session_number) >> 29) & 0x7) == 1;
    // An encrypted payload's CRC is checked after the decryption.
    if (!ell.encrypted) {
      if (!payload_crc_ok(frame, pos))
        return {};
      pos += 2;
    }
  }
  ell.size = (uint8_t) (pos - at);
  return ell;
}

std::optional<Afl> Afl::parse(std::span<const uint8_t> frame, size_t at) {
  if (frame.size() - at < (size_t) ci_length(frame[at]))
    return {};
  size_t pos = at + 2;  // CI and the AFL length
  const auto left = [&] { return frame.size() - pos; };

  const uint16_t fc = frame[pos + 1] << 8 | frame[pos];
  pos += 2;
  const bool has_key_info = fc & 0x0200;
  const bool has_mac = fc & 0x0400;
  const bool has_counter = fc & 0x0800;
  const bool has_control = fc & 0x2000;

  Afl afl{};
  if (has_control) {
    if (left() == 0)
      return {};
    afl.mcl = frame[pos++];
  }
  if (has_key_info) {
    if (left() < 2)
      return {};
    pos += 2;
  }
  if (has_counter) {
    if (left() < 4)
      return {};
    std::copy_n(frame.begin() + pos, afl.counter.size(), afl.counter.begin());
    pos += 4;
  }
  if (has_mac) {
    static const int8_t MAC_LEN[] = {0, 0, 0, 2, 4, 8, 12, 16, 12};
    const int at_len = afl.mcl & 0x0f;
    const int len = at_len < 9 ? MAC_LEN[at_len] : 0;
    if (len == 0 || (int) left() < len)
      return {};
    afl.mac.assign(frame.begin() + pos, frame.begin() + pos + len);
    pos += len;
  }
  afl.size = (uint8_t) (pos - at);
  return afl;
}

std::optional<Tpl> Tpl::parse(std::span<const uint8_t> frame, size_t at) {
  Tpl tpl{.start = (uint16_t) at, .ci = frame[at]};
  size_t pos = at + 1;
  const auto left = [&] { return frame.size() - pos; };

  switch (tpl.ci) {
    case 0x72:
    case 0x73: {
      if (left() < 8)
        return {};
      // id(4) M(2) version type
      const std::span<const uint8_t, 8> b = frame.subspan(pos).first<8>();
      tpl.address = Address{(uint16_t) (b[5] << 8 | b[4]), id_of(b.first<4>()), b[6], b[7]};
      pos += 8;
      [[fallthrough]];
    }
    case 0x7A:
    case 0x7B: {
      if (left() < 4)
        return {};
      tpl.acc = frame[pos++];
      tpl.sts = frame[pos++];
      const uint16_t cfg = frame[pos + 1] << 8 | frame[pos];
      pos += 2;

      tpl.sec_mode = (cfg >> 8) & 0x1f;
      if (tpl.sec_mode == TPL_AES_CBC_IV || tpl.sec_mode == TPL_AES_CBC_NO_IV)
        tpl.num_encr_blocks = (cfg >> 4) & 0x0f;

      if (tpl.sec_mode == TPL_AES_CBC_NO_IV) {
        // Mode 7 extension byte: bits 4-5 select the KDF.
        if (left() == 0)
          return {};
        tpl.kdf = ((frame[pos++] >> 4) & 3) == 1;
      } else if (tpl.sec_mode == TPL_AES_CCM) {
        // Mode 10 extension: tag size, KDF, key id, key version flag; then a 4 byte message counter.
        if (left() < 2)
          return {};
        const uint16_t cfe = frame[pos + 1] << 8 | frame[pos];
        pos += 2;
        CcmHeader ccm{};
        ccm.tag_size = (uint8_t) (4 + ((cfe >> 8) & 0x03) * 4);
        tpl.kdf = ((cfe >> 4) & 0x03) == 1;
        if (((cfe >> 6) & 0x01) != 0) {
          if (left() == 0)
            return {};
          pos++;  // the key version
        }
        // 0xff in the low cfg byte: everything is encrypted.
        if ((cfg & 0xff) != 0xff)
          ccm.encrypted_bytes = (uint8_t) (cfg & 0xff);
        // The tag authenticates the header from the CI up to here; the counter goes into the nonce.
        ccm.aad_len = (uint8_t) (pos - at);
        if (left() < 4)
          return {};
        std::copy_n(frame.begin() + pos, 4, ccm.counter.begin());
        pos += 4;
        tpl.ccm = ccm;
      }
      break;
    }
    default:
      break;
  }
  tpl.size = (uint8_t) (pos - at);
  return tpl;
}

bool Telegram::parse_below_ell() {
  if (left() > 0 && is_ci_of_type(frame[pos_], CiType::NWL)) {
    if (left() < 2)
      return false;
    pos_ += 2;  // CI and the one NWL byte
  }
  if (left() > 0 && is_ci_of_type(frame[pos_], CiType::AFL)) {
    afl = Afl::parse(frame, pos_);
    if (!afl)
      return false;
    pos_ += afl->size;
  }
  if (left() == 0 || !(is_ci_of_type(frame[pos_], CiType::TPL) || is_ci_mfct_specific(frame[pos_])))
    return false;
  tpl = Tpl::parse(frame, pos_);
  if (!tpl)
    return false;
  pos_ += tpl->size;
  if (!carries_records(tpl->ci))
    header_size = (uint16_t) pos_;
  return true;
}

bool Telegram::parse_envelope(std::span<const uint8_t> data) {
  frame.assign(data.begin(), data.end());
  std::copy_n(data.begin(), std::min(data.size(), dll_as_received.size()), dll_as_received.begin());

  quirks::diehl_preprocess(*this);
  const std::optional<Dll> link = Dll::parse(frame);
  if (!link)
    return false;
  dll = *link;
  pos_ = Dll::SIZE;

  if (left() == 0)
    return false;
  if (is_ci_of_type(frame[pos_], CiType::ELL)) {
    ell = Ell::parse(frame, pos_);
    if (!ell)
      return false;
    pos_ += ell->size;
    // The layers below wait for the key.
    if (ell->encrypted)
      return true;
  }
  return parse_below_ell();
}

bool Telegram::check_2f2f() {
  if (left() < 2)
    return false;
  const bool ok = frame[pos_] == 0x2f && frame[pos_ + 1] == 0x2f;
  const bool permitted = quirk_ == PayloadQuirk::Sanxing609B && quirks::sanxing_609b_ok(frame, pos_);
  pos_ += 2;
  return ok || permitted;
}

bool Telegram::potentially_decrypt(Key &key) {
  switch (tpl->sec_mode) {
    case TPL_NO_SECURITY:
      // Unencrypted although a key is configured: rejected, as upstream does.
      return !key;

    case TPL_AES_CBC_IV:
      if (!key)
        quirks::add_default_key(key, *this);
      return aes_cbc_iv_decrypt(std::span(frame).subspan(pos_), key) && check_2f2f();

    case TPL_AES_CBC_NO_IV: {
      if (!tpl->kdf || !key || !afl)
        return false;
      const DerivedKeys derived = derive_keys(*key);
      if (!check_mac(derived))
        return false;
      wmbus::aes_cbc_decrypt(derived.key, std::array<uint8_t, 16>{}, encrypted_blocks(std::span(frame).subspan(pos_)));
      return check_2f2f();
    }

    case TPL_AES_CCM:
      // Set even without a key: the header alone gives the tag size.
      suffix_size = tpl->ccm->tag_size;
      return tpl->kdf && key && aes_ccm_decrypt(derive_keys(*key).key);

    default:
      // 16-31 are manufacturer-specific; only Diehl's real data is read.
      if (tpl->sec_mode >= TPL_SPECIFIC_16_31)
        return !quirks::diehl_is_real_data(*this) || quirks::diehl_decrypt_real_data(*this, pos_, key);
      return false;
  }
}

bool Telegram::parse_compact_apl(CompactFormats &formats) {
  if (left() < 2)
    return false;
  const uint16_t signature = frame[pos_ + 1] << 8 | frame[pos_];
  pos_ += 2;

  // Unknown until a full frame from this meter has arrived.
  const std::optional<std::span<const uint8_t>> format = formats.lookup(signature);
  if (!format || left() < 2)
    return false;
  pos_ += 2;  // the crc of the full frame
  header_size = (uint16_t) pos_;
  read_records(format, formats);
  return true;
}

bool Telegram::decode(Key &key, CompactFormats &formats, std::optional<PayloadQuirk> quirk) {
  quirk_ = quirk;
  if (!tpl) {
    if (!key)
      return false;
    // AES-CTR always "succeeds"; a wrong key yields junk and a bad CRC.
    aes_ctr_decrypt(pos_, *key);
    if (!payload_crc_ok(frame, pos_)) {
      decryption_failed = true;
      return false;
    }
    pos_ += 2;
    if (!parse_below_ell())
      return false;
  }
  if (!carries_records(tpl->ci))
    return true;

  const bool no_header = tpl->ci == 0x78 || tpl->ci == 0x79;
  if (!no_header && !potentially_decrypt(key)) {
    decryption_failed = true;
    return false;
  }

  if (tpl->ci == 0x73 || tpl->ci == 0x79 || tpl->ci == 0x7B)
    return parse_compact_apl(formats);
  header_size = (uint16_t) pos_;
  read_records(std::nullopt, formats);
  return true;
}

std::array<uint8_t, 16> Telegram::tpl_iv() const {
  std::array<uint8_t, 16> iv;
  std::ranges::copy(bytes_of(meter_address()), iv.begin());
  std::fill(iv.begin() + 8, iv.end(), tpl->acc);
  return iv;
}

std::span<uint8_t> Telegram::encrypted_blocks(std::span<uint8_t> buffer) const {
  size_t num = buffer.size();
  if (tpl->num_encr_blocks)
    num = std::min<size_t>(tpl->num_encr_blocks * 16, num);
  return buffer.first(num - num % 16);
}

bool Telegram::aes_cbc_iv_decrypt(std::span<uint8_t> buffer, const Key &key) const {
  if (!key)
    return false;
  const std::span<uint8_t> blocks = encrypted_blocks(buffer);
  if (blocks.empty())
    return false;
  wmbus::aes_cbc_decrypt(*key, tpl_iv(), blocks);
  return true;
}

// The 13 byte mode 10 nonce: M(2) ID(4) Ver(1) Type(1) 00 and the message
// counter, which travels little endian but enters the nonce big endian.
std::array<uint8_t, 13> Telegram::ccm_nonce() const {
  std::array<uint8_t, 13> nonce;
  std::ranges::copy(bytes_of(meter_address()), nonce.begin());
  nonce[8] = 0x00;
  std::ranges::reverse_copy(tpl->ccm->counter, nonce.begin() + 9);
  return nonce;
}

// RFC 3610's counter block A_i: flags 01, the nonce, i.
static std::array<uint8_t, 16> ccm_counter_block(std::span<const uint8_t, 13> nonce, uint16_t i) {
  std::array<uint8_t, 16> a{0x01};
  std::ranges::copy(nonce, a.begin() + 1);
  a[14] = (uint8_t) (i >> 8);
  a[15] = (uint8_t) i;
  return a;
}

// RFC 3610 CBC-MAC with a two byte length field, masked with S0.
std::array<uint8_t, 16> Telegram::ccm_tag(std::span<const uint8_t, 16> key, std::span<const uint8_t, 13> nonce,
                                          std::span<const uint8_t> plain) const {
  std::array<uint8_t, 16> x{};
  size_t n = 0;
  const auto absorb = [&](uint8_t b) {
    x[n++] ^= b;
    if (n < x.size())
      return;
    x = wmbus::aes_ecb_encrypt(key, x);
    n = 0;
  };
  const auto pad = [&]() {
    while (n != 0)
      absorb(0);
  };

  absorb((uint8_t) (0x40 | ((tpl->ccm->tag_size - 2) / 2) << 3 | 0x01));
  for (uint8_t b : nonce)
    absorb(b);
  absorb((uint8_t) (plain.size() >> 8));
  absorb((uint8_t) plain.size());

  const std::span<const uint8_t> aad = std::span(frame).subspan(tpl->start, tpl->ccm->aad_len);
  absorb((uint8_t) (aad.size() >> 8));
  absorb((uint8_t) aad.size());
  for (uint8_t b : aad)
    absorb(b);
  pad();
  for (uint8_t b : plain)
    absorb(b);
  pad();

  xor_into(x, wmbus::aes_ecb_encrypt(key, ccm_counter_block(nonce, 0)));
  return x;
}

// Mode 10 (OMS profile D): AES-CTR, keystream block i = E(key, 01 || nonce || i), i from 1.
bool Telegram::aes_ccm_decrypt(std::span<const uint8_t, 16> key) {
  const std::span<uint8_t> rest = std::span(frame).subspan(pos_);
  if (rest.size() < tpl->ccm->tag_size)
    return false;
  const std::span<uint8_t> body = rest.first(rest.size() - tpl->ccm->tag_size);
  const std::span<uint8_t> cipher =
      tpl->ccm->encrypted_bytes ? body.first(std::min<size_t>(*tpl->ccm->encrypted_bytes, body.size())) : body;

  const std::array<uint8_t, 13> nonce = ccm_nonce();
  for (size_t off = 0; off < cipher.size(); off += 16)
    xor_into(cipher.subspan(off), wmbus::aes_ecb_encrypt(key, ccm_counter_block(nonce, (uint16_t) (off / 16 + 1))));

  // Mode 10 has no 2f2f: the tag checks both key and integrity. A mismatch is still read, marked.
  const std::array<uint8_t, 16> computed = ccm_tag(key, nonce, cipher);
  if (!std::equal(rest.end() - tpl->ccm->tag_size, rest.end(), computed.begin()))
    decoding_errors.add("FAILED_DECODE");
  return true;
}

void Telegram::aes_ctr_decrypt(size_t pos, std::span<const uint8_t, 16> key) {
  std::array<uint8_t, 16> iv{};
  std::ranges::copy(bytes_of(dll.address), iv.begin());
  iv[8] = ell->cc & ~(0x10) & ~(0x02);  // without the hop count and repeated access bits
  std::ranges::copy(ell->session_number, iv.begin() + 9);

  for (size_t off = pos; off < frame.size(); off += 16) {
    xor_into(std::span(frame).subspan(off), wmbus::aes_ecb_encrypt(key, iv));
    for (int p = 15; p >= 0; --p) {
      if (++iv[p] != 0)
        break;
    }
  }
}

bool Telegram::check_mac(const DerivedKeys &derived) const {
  if (afl->mac.empty())
    return false;

  std::vector<uint8_t> input{afl->mcl};
  input.insert(input.end(), afl->counter.begin(), afl->counter.end());
  input.insert(input.end(), frame.begin() + tpl->start, frame.end());

  const std::array<uint8_t, 16> mac = wmbus::aes_cmac(derived.mac_key, input);
  return std::equal(afl->mac.begin(), afl->mac.end(), mac.begin());
}

DerivedKeys Telegram::derive_keys(std::span<const uint8_t, 16> key) const {
  std::array<uint8_t, 16> input;
  int n = 0;
  input[n++] = 0x00;  // DC 00: the encryption key
  // Mode 10 counts its messages in the TPL header, mode 7 in the AFL.
  for (uint8_t b : tpl->ccm ? tpl->ccm->counter : afl->counter)
    input[n++] = b;
  const uint32_t id = meter_address().id;
  for (int shift = 0; shift < 32; shift += 8)
    input[n++] = (uint8_t) (id >> shift);
  for (int i = 0; i < 7; ++i)
    input[n++] = 0x07;

  DerivedKeys derived;
  derived.key = wmbus::aes_cmac(key, input);
  input[0] = 0x01;  // DC 01: the MAC key
  derived.mac_key = wmbus::aes_cmac(key, input);
  return derived;
}

static bool crc_ok(std::span<const uint8_t> data, std::span<const uint8_t, 2> crc_bytes) {
  uint16_t calc = wmbus::crc16_en13757(data);
  uint16_t check = crc_bytes[0] << 8 | crc_bytes[1];
  return calc == check;
}

// Format A: a 10-byte block + CRC, then 16-byte blocks + CRC, the last one shorter.
// All CRCs are checked before a byte moves, so a failure leaves the buffer intact for format B.
static bool trim_crcs_format_a(std::vector<uint8_t> &payload) {
  const std::span<const uint8_t> bytes{payload};
  const size_t len = bytes.size();
  if (len < 12)
    return false;
  if (!crc_ok(bytes.first(10), bytes.subspan(10).first<2>()))
    return false;

  size_t pos = 12;
  for (; pos + 18 <= len; pos += 18) {
    if (!crc_ok(bytes.subspan(pos, 16), bytes.subspan(pos + 16).first<2>()))
      return false;
  }
  const size_t tail = pos + 2 < len ? len - 2 - pos : 0;
  if (tail > 0 && !crc_ok(bytes.subspan(pos, tail), bytes.subspan(pos + tail).first<2>()))
    return false;

  size_t out = 10;
  for (pos = 12; pos + 18 <= len; pos += 18) {
    memmove(payload.data() + out, payload.data() + pos, 16);
    out += 16;
  }
  memmove(payload.data() + out, payload.data() + pos, tail);
  out += tail;
  payload.resize(out);
  payload[0] = (uint8_t) (out - 1);
  return true;
}

// Format B: one CRC at the end, plus a second after byte 126 when over 128 bytes.
static bool trim_crcs_format_b(std::vector<uint8_t> &payload) {
  const std::span<const uint8_t> bytes{payload};
  const size_t len = bytes.size();
  if (len < 12)
    return false;

  size_t crc1_pos, crc2_pos;
  if (len <= 128) {
    crc1_pos = len - 2;
    crc2_pos = 0;
  } else {
    crc1_pos = 126;
    crc2_pos = len - 2;
  }

  if (!crc_ok(bytes.first(crc1_pos), bytes.subspan(crc1_pos).first<2>()))
    return false;
  size_t out = crc1_pos;
  if (crc2_pos > 0) {
    size_t from2 = crc1_pos + 2;
    if (!crc_ok(bytes.subspan(from2, crc2_pos - from2), bytes.subspan(crc2_pos).first<2>()))
      return false;
    memmove(payload.data() + out, payload.data() + from2, crc2_pos - from2);
    out += crc2_pos - from2;
  }
  payload.resize(out);
  payload[0] = (uint8_t) (out - 1);
  return true;
}

bool remove_dll_crcs(std::vector<uint8_t> &payload) {
  return trim_crcs_format_a(payload) || trim_crcs_format_b(payload);
}

// SND_NR and SND_IR; nothing else has been seen.
static bool is_valid_wmbus_c_field(uint8_t c) { return c == 0x44 || c == 0x46; }

bool is_complete_frame(std::span<const uint8_t> data) { return data.size() >= 11 && is_valid_wmbus_c_field(data[1]); }

void Telegram::read_records(std::optional<std::span<const uint8_t>> format, CompactFormats &formats) {
  records = parse_records(frame, payload(), format);
  if (format)
    return;
  // Its CRC is the signature later compact frames arrive under; upstream leaves the 0x0f marker out.
  std::vector<uint8_t> layout;
  for (const Record &r : records) {
    if (r.dif != 0x0f)
      layout.insert(layout.end(), r.id.begin(), r.id.end());
  }
  formats.learn(layout);
}

IxmlEntry &Telegram::ixml_entry_for(std::span<const uint8_t> key) {
  const auto held = std::ranges::find_if(ixml_entries, [key](const IxmlEntry &e) { return std::ranges::equal(e.id, key); });
  return held != ixml_entries.end() ? *held : ixml_entries.emplace_back();
}

bool Telegram::add_ixml_records(int base_offset, std::span<const uint8_t> bytes, const IxmlGrammar &grammar) {
  const std::optional<std::vector<IxmlMatch>> found = grammar.decode(bytes);
  if (!found)
    return false;

  for (const IxmlMatch &match : *found) {
    // A frame entry under the same key is not looked for here; the walk substitutes it.
    IxmlEntry &e = ixml_entry_for(match.key);
    e.id = match.key;
    e.data = match.data;
    e.offset = (uint16_t) (base_offset + (match.data.data() - bytes.data()));
  }
  return true;
}

void Status::add(std::string_view text) {
  size_t start = 0;
  while (start <= text.size()) {
    size_t end = text.find(' ', start);
    if (end == std::string_view::npos)
      end = text.size();
    const std::string_view flag = text.substr(start, end - start);
    start = end + 1;
    if (flag.empty())
      continue;
    if (flag == "OK") {
      ok_ = true;
      continue;
    }
    const auto at = std::lower_bound(flags_.begin(), flags_.end(), flag);
    if (at == flags_.end() || *at != flag)
      flags_.insert(at, std::string(flag));
  }
}

void Status::add(const Status &other) {
  ok_ |= other.ok_;
  for (const std::string &flag : other.flags_)
    add(flag);
}

std::string Status::str() const {
  if (flags_.empty())
    return ok_ ? "OK" : "";
  std::string result;
  for (const std::string &flag : flags_) {
    if (!result.empty())
      result += ' ';
    result += flag;
  }
  // Only for em24's deprecated error field.
  std::replace(result.begin(), result.end(), '~', ' ');
  return result;
}

}  // namespace wmbus
