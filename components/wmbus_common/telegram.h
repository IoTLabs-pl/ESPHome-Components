#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ixml.h"
#include "records.h"

namespace wmbus {

// The L byte plus the 255 bytes it can count.

// Per meter; the oldest is overwritten.
constexpr size_t LEARNED_FORMATS_KEPT = 4;

// EN 13757-7 TPL cfg modes. DES (2, 3) is left out: it needs CONFIG_MBEDTLS_DES_C firmware-wide.
enum TplSecurityMode : uint8_t {
  TPL_NO_SECURITY = 0,
  TPL_AES_CBC_IV = 5,
  TPL_AES_CBC_NO_IV = 7,
  TPL_AES_CCM = 10,
  TPL_SPECIFIC_16_31 = 16,
};

constexpr uint16_t manufacturer_code(char a, char b, char c) {
  return (uint16_t) (((a - 64) << 10) | ((b - 64) << 5) | (c - 64));
}

struct Telegram;

using Key = std::optional<std::array<uint8_t, 16>>;

// Declared by the driver, plus those learned from this meter's full frames; a compact frame
// names its format by the format's CRC.
struct CompactFormats {
  std::span<const std::span<const uint8_t>> registered;
  std::vector<std::vector<uint8_t>> learned;
  uint8_t learned_next = 0;

  std::optional<std::span<const uint8_t>> lookup(uint16_t signature) const;
  void learn(std::span<const uint8_t> bytes);
};

// No dif/vif in the frame; `id` points into the grammar's key bytes, `data` into the telegram.
struct IxmlEntry {
  std::span<const uint8_t> id;
  uint16_t offset;
  std::span<const uint8_t> data;
};

// Sorted, unique flags. "OK" is shown only when no other flag is set.
class Status {
 public:
  // Space-separated flags.
  void add(std::string_view text);
  void add(const Status &other);
  bool empty() const { return flags_.empty(); }
  std::string str() const;

 private:
  std::vector<std::string> flags_;
  bool ok_ = false;
};

// Mode 10 cfg extension; encrypted_bytes is empty when the whole payload is encrypted.
struct CcmHeader {
  std::array<uint8_t, 4> counter;
  uint8_t tag_size;
  uint8_t aad_len;
  std::optional<uint8_t> encrypted_bytes;
};

// Named by a driver's `transform_payload`; see quirks.h.
enum class PayloadQuirk : uint8_t {
  DiehlPrios,
  Sanxing609B,
  TryQundisDecode,
};

struct Address {
  uint16_t mfct;
  uint32_t id;
  uint8_t version;
  uint8_t type;

  // M(2) id(4) version type: the DLL and ELL order, and the one IVs and nonces take.
  static Address parse(std::span<const uint8_t, 8> bytes);
};

// Each layer's parse reads the layer whose CI is at `at`; nullopt when it is malformed.

struct Dll {
  static constexpr size_t SIZE = 10;  // L, C, address
  Address address;

  static std::optional<Dll> parse(std::span<const uint8_t> frame);
};

struct Ell {
  uint8_t cc;
  std::optional<Address> address;  // CI 8E and 8F
  std::array<uint8_t, 4> session_number = {};
  bool encrypted = false;
  uint8_t size = 0;  // up to the payload, and its CRC unless encrypted

  static std::optional<Ell> parse(std::span<const uint8_t> frame, size_t at);
};

struct Afl {
  uint8_t mcl = 0;
  std::array<uint8_t, 4> counter = {};
  std::vector<uint8_t> mac;
  uint8_t size = 0;

  static std::optional<Afl> parse(std::span<const uint8_t> frame, size_t at);
};

struct Tpl {
  uint16_t start;
  uint8_t ci;
  std::optional<Address> address;  // the long header only
  uint8_t acc = 0;
  uint8_t sts = 0;
  uint8_t sec_mode = TPL_NO_SECURITY;
  uint8_t num_encr_blocks = 0;
  bool kdf = false;              // mode 7 and 10 with derived keys
  std::optional<CcmHeader> ccm;  // mode 10 only
  uint8_t size = 0;

  static std::optional<Tpl> parse(std::span<const uint8_t> frame, size_t at);
};

struct DerivedKeys {
  std::array<uint8_t, 16> key;
  std::array<uint8_t, 16> mac_key;
};

struct Telegram {
  std::vector<uint8_t> frame;
  // Before diehl_preprocess rearranges it.
  std::array<uint8_t, Dll::SIZE> dll_as_received = {};

  Dll dll = {};
  std::optional<Ell> ell;
  std::optional<Afl> afl;
  // Parsed by decode when the ELL is encrypted.
  std::optional<Tpl> tpl;

  uint16_t header_size = 0;
  uint16_t suffix_size = 0;

  bool decryption_failed = false;
  Status decoding_errors;

  std::vector<Record> records;
  // Only ixml drivers fill them.
  std::vector<IxmlEntry> ixml_entries;

  // Payloads outside the frame; one buffer per producer, since ixml entries point into them.
  std::vector<std::vector<uint8_t>> derived;

  std::span<const uint8_t> payload() const;

  // The layers up to and including the addresses.
  bool parse_envelope(std::span<const uint8_t> data);
  // `key` may be replaced by a manufacturer default key.
  bool decode(Key &key, CompactFormats &formats, std::optional<PayloadQuirk> quirk);

  bool add_ixml_records(int base_offset, std::span<const uint8_t> bytes, const IxmlGrammar &grammar);

  // The deepest layer's address.
  const Address &sender() const;
  // wmbusmeters naming, e.g. "water".
  const char *media_type() const;

  // Mode 5; drivers' transform_payload uses it too.
  bool aes_cbc_iv_decrypt(std::span<uint8_t> buffer, const Key &key) const;
  std::array<uint8_t, 16> tpl_iv() const;

 private:
  size_t pos_ = 0;
  std::optional<PayloadQuirk> quirk_;

  // What the TPL encryption binds to: the long header's address, else the DLL's.
  const Address &meter_address() const;

  size_t left() const;
  bool parse_below_ell();
  bool potentially_decrypt(Key &key);
  bool check_2f2f();
  bool parse_compact_apl(CompactFormats &formats);

  // An entry with the same key is overwritten, as upstream does.
  IxmlEntry &ixml_entry_for(std::span<const uint8_t> key);
  // Also learns a full frame's layout for later compact frames.
  void read_records(std::optional<std::span<const uint8_t>> format, CompactFormats &formats);

  std::array<uint8_t, 13> ccm_nonce() const;
  std::array<uint8_t, 16> ccm_tag(std::span<const uint8_t, 16> key, std::span<const uint8_t, 13> nonce,
                                  std::span<const uint8_t> plain) const;
  bool aes_ccm_decrypt(std::span<const uint8_t, 16> key);
  std::span<uint8_t> encrypted_blocks(std::span<uint8_t> buffer) const;
  void aes_ctr_decrypt(size_t pos, std::span<const uint8_t, 16> key);
  bool check_mac(const DerivedKeys &derived) const;
  DerivedKeys derive_keys(std::span<const uint8_t, 16> key) const;
};

// For a buffer after remove_dll_crcs: only the C field is left to check.
bool is_complete_frame(std::span<const uint8_t> data);

// Format A has a CRC every 16 bytes, format B at the end; false when neither holds.
bool remove_dll_crcs(std::vector<uint8_t> &payload);

}  // namespace wmbus
