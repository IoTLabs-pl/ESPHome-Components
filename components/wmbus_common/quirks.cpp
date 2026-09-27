#include "quirks.h"

#include <algorithm>
#include <cstring>
#include <numeric>

#include "crypto.h"
#include "esphome/core/helpers.h"

namespace wmbus::quirks {

enum class DiehlFrameInterpretation { NA, REAL_DATA, OMS, PRIOS, SAP_PRIOS, SAP_PRIOS_STD, PRIOS_SCR, RESERVED };

static bool is_diehl_manufacturer(int m) {
  return m == manufacturer_code('D', 'M', 'E') || m == manufacturer_code('E', 'W', 'T') ||
         m == manufacturer_code('H', 'Y', 'D') || m == manufacturer_code('S', 'A', 'P') ||
         m == manufacturer_code('S', 'P', 'L');
}

static DiehlFrameInterpretation diehl_interpretation(uint8_t c_field, int m_field, uint8_t ci_field, int tpl_cfg) {
  if (!is_diehl_manufacturer(m_field))
    return DiehlFrameInterpretation::NA;
  if (c_field != 0x44 && c_field != 0x46)
    return DiehlFrameInterpretation::NA;

  switch (ci_field) {
    case 0x71:
      return DiehlFrameInterpretation::REAL_DATA;
    case 0x7A:
      if (((tpl_cfg >> 8) & 0x10) == 0x10)
        return DiehlFrameInterpretation::REAL_DATA;
      return DiehlFrameInterpretation::OMS;
    case 0xA0:
    case 0xA1:
    case 0xA2:
    case 0xA3:
    case 0xA4:
    case 0xA5:
    case 0xA6:
    case 0xA7:
      if (m_field == manufacturer_code('S', 'A', 'P'))
        return DiehlFrameInterpretation::SAP_PRIOS;
      return DiehlFrameInterpretation::PRIOS;
    case 0xB0:
      if (m_field == manufacturer_code('S', 'A', 'P'))
        return DiehlFrameInterpretation::SAP_PRIOS_STD;
      return DiehlFrameInterpretation::RESERVED;
    case 0xA8:
    case 0xA9:
    case 0xAA:
    case 0xAB:
    case 0xAC:
    case 0xAD:
    case 0xAE:
    case 0xAF:
    case 0xB4:
    case 0xB5:
    case 0xB6:
    case 0xB7:
      return DiehlFrameInterpretation::RESERVED;
    case 0xB1:
    case 0xB2:
    case 0xB3:
      return DiehlFrameInterpretation::PRIOS_SCR;
    default:
      return DiehlFrameInterpretation::OMS;
  }
}

static DiehlFrameInterpretation diehl_interpretation(const Telegram &t) {
  if (t.frame.size() < 15)
    return DiehlFrameInterpretation::NA;
  const std::span<const uint8_t> f = t.frame;
  return diehl_interpretation(f[1], f[3] << 8 | f[2], f[10], f[14] << 8 | f[13]);
}

void diehl_preprocess(Telegram &t) {
  DiehlFrameInterpretation fi = diehl_interpretation(t);
  bool swap = fi == DiehlFrameInterpretation::PRIOS || fi == DiehlFrameInterpretation::PRIOS_SCR ||
              fi == DiehlFrameInterpretation::REAL_DATA;
  bool sap = fi == DiehlFrameInterpretation::SAP_PRIOS;
  if (!swap && !sap)
    return;

  if (swap) {
    uint8_t version = t.frame[4];
    uint8_t type = t.frame[5];
    for (int i = 4; i < 8; i++)
      t.frame[i] = t.frame[i + 2];
    t.frame[8] = version;
    t.frame[9] = type;
  } else {
    t.frame[8] = 0x00;  // on an Izar the version is part of the id
    t.frame[9] = 0x07;  // water meter
  }
}

static uint32_t uint32_from_bytes(std::span<const uint8_t> data, size_t offset) {
  return ((uint32_t) data[offset] << 24) | ((uint32_t) data[offset + 1] << 16) | ((uint32_t) data[offset + 2] << 8) |
         (uint32_t) data[offset + 3];
}

static uint32_t convert_key(std::span<const uint8_t, 8> bytes) {
  return uint32_from_bytes(bytes, 0) ^ uint32_from_bytes(bytes, 4);
}

static const uint8_t PRIOS_DEFAULT_KEY1[8] = {0x39, 0xBC, 0x8A, 0x10, 0xE6, 0x6D, 0x83, 0xF8};
static const uint8_t PRIOS_DEFAULT_KEY2[8] = {0x51, 0x72, 0x89, 0x10, 0xE6, 0x6D, 0x83, 0xF8};

static std::vector<uint32_t> diehl_candidates(const Key &key) {
  if (key)
    return {convert_key(std::span(*key).first<8>())};
  return {convert_key(PRIOS_DEFAULT_KEY1), convert_key(PRIOS_DEFAULT_KEY2)};
}

enum class DiehlLfsrCheck { CHECKSUM_AND_0XEF, HEADER_1_BYTE };

// Empty when the key does not fit.
static std::vector<uint8_t> decode_diehl_lfsr(std::span<const uint8_t> origin, std::span<const uint8_t> frame,
                                              uint32_t key, DiehlLfsrCheck check, uint32_t check_value) {
  if (frame.size() < 15)
    return {};
  key ^= uint32_from_bytes(origin, 2);
  key ^= uint32_from_bytes(origin, 6);
  key ^= uint32_from_bytes(frame, 10);

  std::vector<uint8_t> out(frame.size() - 15);
  for (size_t i = 0; i < out.size(); ++i) {
    for (int j = 0; j < 8; ++j) {
      uint8_t bit = ((key & 0x2) != 0) ^ ((key & 0x4) != 0) ^ ((key & 0x800) != 0) ^ ((key & 0x80000000) != 0);
      key = (key << 1) | bit;
    }
    out[i] = frame[i + 15] ^ (key & 0xFF);

    if (check == DiehlLfsrCheck::HEADER_1_BYTE) {
      if (out[0] != 0x4B)
        return {};
    } else if (i == out.size() - 1) {
      if ((std::accumulate(out.begin(), out.end(), 0u) & 0xEF) != check_value)
        return {};
    }
  }
  return out;
}

void add_default_key(Key &key, const Telegram &t) {
  if (!key && t.tpl->sec_mode == TPL_AES_CBC_IV && diehl_interpretation(t) == DiehlFrameInterpretation::OMS) {
    key.emplace();
    memcpy(key->data(), PRIOS_DEFAULT_KEY2, 8);
    memcpy(key->data() + 8, PRIOS_DEFAULT_KEY2, 8);
  }
}

bool diehl_decrypt_real_data(Telegram &t, size_t pos, const Key &key) {
  std::vector<uint8_t> decoded;
  for (uint32_t candidate : diehl_candidates(key)) {
    decoded = decode_diehl_lfsr(t.dll_as_received, t.frame, candidate, DiehlLfsrCheck::CHECKSUM_AND_0XEF,
                                t.frame[14] & 0xEF);
    if (!decoded.empty())
      break;
  }
  if (decoded.empty())
    return false;
  t.frame.resize(pos + decoded.size());
  std::copy(decoded.begin(), decoded.end(), t.frame.begin() + pos);
  return true;
}

bool diehl_prios_decode(Telegram &t, const Key &key) {
  std::vector<uint8_t> decoded;
  for (uint32_t candidate : diehl_candidates(key)) {
    decoded = decode_diehl_lfsr(t.dll_as_received, t.frame, candidate, DiehlLfsrCheck::HEADER_1_BYTE, 0x4B);
    if (!decoded.empty())
      break;
  }
  if (decoded.empty())
    return false;

  // ixml expects four bytes past the header, then the decoded payload.
  const size_t hs = t.header_size;
  const size_t head = std::min<size_t>(4, t.frame.size() > hs ? t.frame.size() - hs : 0);
  std::vector<uint8_t> &out = t.derived.emplace_back(t.frame.begin() + hs, t.frame.begin() + hs + head);
  out.insert(out.end(), decoded.begin(), decoded.end());
  return true;
}

std::optional<SapPriosIdentity> sap_prios_identity(const Telegram &t) {
  if (diehl_interpretation(t) != DiehlFrameInterpretation::SAP_PRIOS)
    return {};
  const std::span<const uint8_t> o = t.dll_as_received;
  const uint32_t number = ((uint32_t) (o[7] & 0x03) << 24) | ((uint32_t) o[6] << 16) | ((uint32_t) o[5] << 8) | o[4];
  // Eight digits: two-digit year of manufacture, then the serial number.
  const uint32_t yy = (number / 1000000) % 100;
  const uint32_t serial = number % 1000000;
  const char supplier_code = (char) ('@' + (((o[9] & 0x0F) << 1) | (o[8] >> 7)));
  const char meter_type = (char) ('@' + ((o[8] & 0x7C) >> 2));
  const char diameter = (char) ('@' + (((o[8] & 0x03) << 3) | (o[7] >> 5)));
  return SapPriosIdentity{
      esphome::str_sprintf("%c%02u%c%c", supplier_code, (unsigned) yy, meter_type, diameter),
      esphome::str_sprintf("%06u", (unsigned) serial),
      esphome::str_sprintf("%u", (unsigned) (yy > 70 ? 1900 + yy : 2000 + yy)),
  };
}

bool diehl_is_real_data(const Telegram &t) { return diehl_interpretation(t) == DiehlFrameInterpretation::REAL_DATA; }

// 0x609b is a marker, not noise: captures under different access counters all carry it.
bool sanxing_609b_ok(std::span<const uint8_t> frame, size_t pos) {
  return frame[pos] == 0x60 && frame[pos + 1] == 0x9b && frame[frame.size() - 2] == 0x2f &&
         frame[frame.size() - 1] == 0x2f;
}

// Qundis Q 5.5 walk-by block (0DFF5F): mode 5 under the meter key when byte [4] is 0x35 (plaintext: 0x00).
// A CI 0x78 frame has no TPL header, so the IV's access number is the block's rolling counter in byte [2].
std::optional<std::vector<uint8_t>> qundis_walk_by_decode(Telegram &t, const Key &key, std::span<const uint8_t> block) {
  if (block.size() < 9 || block[0] != 0x00 || block[1] != 0x82 || block[4] != 0x35)
    return {};
  if (!key) {
    // The grammar's header guard then rejects the ciphertext rather than decode it.
    t.decoding_errors.add("MISSING_KEY");
    return {};
  }
  const std::span<const uint8_t> cipher = block.subspan(5);
  if (cipher.empty() || cipher.size() % 16 != 0)
    return {};

  t.tpl->acc = block[2];
  std::array<uint8_t, 16> iv = t.tpl_iv();
  // With an enhanced identification record (0779), the block is encrypted for that id, not the converter's.
  for (const Record &r : t.records) {
    if (r.dif == 0x07 && r.vif == 0x79 && r.bytes().size() >= 8) {
      std::span<const uint8_t> b = r.bytes();
      iv[0] = b[4];
      iv[1] = b[5];
      iv[2] = b[0];
      iv[3] = b[1];
      iv[4] = b[2];
      iv[5] = b[3];
      iv[6] = b[6];
      iv[7] = b[7];
      break;
    }
  }

  std::vector<uint8_t> plain(block.begin(), block.end());
  // A plaintext block has 0x00 here.
  plain[4] = 0x00;
  std::span<uint8_t> blocks = std::span(plain).subspan(5);
  wmbus::aes_cbc_decrypt(*key, iv, blocks);
  return plain;
}

}  // namespace wmbus::quirks
