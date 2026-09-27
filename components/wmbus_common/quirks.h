// Diehl's quirks are frame-level: its header must be fixed before the address is readable.
// The rest are named by a driver's `transform_payload` and reach the decoder as PayloadQuirk.

#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "telegram.h"

namespace wmbus::quirks {

// As izarv2 prints it.
struct SapPriosIdentity {
  std::string prefix;
  std::string serial_number;
  std::string manufacture_y;
};

// Restores Diehl's rearranged DLL header; runs on every frame.
void diehl_preprocess(Telegram &t);

// A Diehl frame whose payload is the LFSR stream rather than records.
bool diehl_is_real_data(const Telegram &t);
// Unrolls that payload over the frame in place; false when no key fits.
bool diehl_decrypt_real_data(Telegram &t, size_t pos, const Key &key);

// An OMS frame from a Diehl meter is encrypted with a published key.
void add_default_key(Key &key, const Telegram &t);

// PRIOS: unrolls the LFSR payload into `derived`; false when no key fits.
bool diehl_prios_decode(Telegram &t, const Key &key);
std::optional<SapPriosIdentity> sap_prios_identity(const Telegram &t);

// Sanxing S34U28: a fixed 0x609b marker replaces the 2f2f check; trailing 2f2f padding checks the key.
bool sanxing_609b_ok(std::span<const uint8_t> frame, size_t pos);

// Nothing when the block is not encrypted; MISSING_KEY in the status when there is no key.
std::optional<std::vector<uint8_t>> qundis_walk_by_decode(Telegram &t, const Key &key, std::span<const uint8_t> block);

}  // namespace wmbus::quirks
