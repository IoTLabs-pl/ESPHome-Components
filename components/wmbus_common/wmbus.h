/*
 Copyright (C) 2017-2022 Fredrik Öhrström (gpl-3.0-or-later)

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef WMBUS_H
#define WMBUS_H

#include"address.h"
#include"dvparser.h"
#include"manufacturers.h"
#include"translatebits.h"
#include"util.h"

#include<inttypes.h>
#include<map>
#include<set>

// Check and remove the data link layer CRCs from a wmbus telegram.
// If the CRCs do not pass the test, return false.
void removeAnyDLLCRCs(std::vector<uchar> &payload);
bool trimCRCsFrameFormatA(std::vector<uchar> &payload);
bool trimCRCsFrameFormatB(std::vector<uchar> &payload);

#define LIST_OF_MBUS_DEVICES \
    X(UNKNOWN,unknown,false,false,detectUNKNOWN)     \
    X(MBUS,mbus,true,false,detectMBUS)               \
    X(AUTO,auto,false,false,detectAUTO)              \
    X(AMB8465,amb8465,true,false,detectAMB8465AMB3665)\
    X(AMB3665,amb3665,true,false,detectSKIP)         \
    X(CUL,cul,true,false,detectCUL)                  \
    X(IM871A,im871a,true,false,detectIM871AIM170A)   \
    X(IM170A,im170a,true,false,detectSKIP)           \
    X(IU891A,iu891a,true,false,detectIU891A)         \
    X(RAWTTY,rawtty,true,false,detectRAWTTY)         \
    X(HEXTTY,hextty,true,false,detectSKIP)           \
    X(RC1180,rc1180,true,false,detectRC1180)         \
    X(RTL433,rtl433,false,true,detectRTL433)         \
    X(RTLWMBUS,rtlwmbus,false,true,detectRTLWMBUS)   \
    X(IU880B,iu880b,true,false,detectSKIP)         \
    X(SIMULATION,simulation,false,false,detectSIMULATION)

enum BusDeviceType {
#define X(name,text,tty,rtlsdr,detector) DEVICE_ ## name,
LIST_OF_MBUS_DEVICES
#undef X
};

enum class TelegramFormat
{
    UNKNOWN,
    WMBUS_C_FIELD, // The payload begins with the c-field
    WMBUS_CI_FIELD, // The payload begins with the ci-field (ie the c-field + dll is auto-prefixed.)
    MBUS_SHORT_FRAME, // Short mbus frame (ie ack etc)
    MBUS_LONG_FRAME // Long mbus frame (ie data frame)
};
const char *toString(TelegramFormat format);
TelegramFormat toTelegramFormat(const char *s);

enum class DeviceMode
{
    UNKNOWN,
    OTHER,
    METER
};
const char *toString(DeviceMode mode);
DeviceMode toDeviceMode(const char *s);

void setIgnoreDuplicateTelegrams(bool idt);
void setDetailedFirst(bool df);
bool getDetailedFirst();

// In link mode S1, is used when both the transmitter and receiver are stationary.
// It can be transmitted relatively seldom.

// In link mode T1, the meter transmits a telegram every few seconds or minutes.
// Suitable for drive-by/walk-by collection of meter values.

// Link mode C1 is like T1 but uses less energy when transmitting due to
// a different radio encoding. Also significant is:
// S1/T1 usually uses the A format for the data link layer, more CRCs.
// C1 usually uses the B format for the data link layer, less CRCs = less overhead.

// The im871a can for example receive C1a, but it is unclear if there are any meters that use it.

#define LIST_OF_LINK_MODES \
    X(Any,any,--anylinkmode,(~0UL)) \
    X(MBUS,mbus,--mbus,(1UL<<1))    \
    X(S1,s1,--s1,      (1UL<<2))    \
    X(S1m,s1m,--s1m,   (1UL<<3))    \
    X(S2,s2,--s2,      (1UL<<4))    \
    X(T1,t1,--t1,      (1UL<<5))    \
    X(T2,t2,--t2,      (1UL<<6))    \
    X(C1,c1,--c1,      (1UL<<7))    \
    X(C2,c2,--c2,      (1UL<<8))    \
    X(N1a,n1a,--n1a,   (1UL<<9))    \
    X(N2a,n2a,--n2a,   (1UL<<10))    \
    X(N1b,n1b,--n1b,   (1UL<<11))    \
    X(N2b,n2b,--n2b,   (1UL<<12))    \
    X(N1c,n1c,--n1c,   (1UL<<13))    \
    X(N2c,n2c,--n2c,   (1UL<<14))    \
    X(N1d,n1d,--n1d,   (1UL<<15))    \
    X(N2d,n2d,--n2d,   (1UL<<16))    \
    X(N1e,n1e,--n1e,   (1UL<<17))    \
    X(N2e,n2e,--n2e,   (1UL<<18))    \
    X(N1f,n1f,--n1f,   (1UL<<19))    \
    X(N2f,n2f,--n2f,   (1UL<<20))    \
    X(R2a,r2a,--r2a,   (1UL<<21))    \
    X(R2b,r2b,--r2b,   (1UL<<22))    \
    X(R2c,r2c,--r2c,   (1UL<<23))    \
    X(R2d,r2d,--r2d,   (1UL<<24))    \
    X(R2e,r2e,--r2e,   (1UL<<25))    \
    X(R2f,r2f,--r2f,   (1UL<<26))    \
    X(R2g,r2g,--r2g,   (1UL<<27))    \
    X(R2h,r2h,--r2h,   (1UL<<28))    \
    X(R2i,r2i,--r2i,   (1UL<<29))    \
    X(R2j,r2j,--r2j,   (1UL<<30))    \
    X(LORA,lora,--lora,   (1UL<<31))    \
    X(UNKNOWN,unknown,----,0x0UL)

enum class LinkMode {
#define X(name,lcname,option,val) name,
LIST_OF_LINK_MODES
#undef X
};

#define X(name,lcname,option,val) const uint64_t name##_bit = val;
LIST_OF_LINK_MODES
#undef X

LinkMode toLinkMode(const char *arg);
LinkMode isLinkModeOption(const char *arg);
const char *toString(LinkMode lm);

struct LinkModeSet
{
    // Add the link mode to the set of link modes.
    LinkModeSet &addLinkMode(LinkMode lm);
    void unionLinkModeSet(LinkModeSet lms);
    void disjunctionLinkModeSet(LinkModeSet lms);
    // Does this set support listening to the given link mode set?
    // If this set is C1 and T1 and the supplied set contains just C1,
    // then supports returns true.
    // Or if this set is just T1 and the supplied set contains just C1,
    // then supports returns false.
    // Or if this set is just C1 and the supplied set contains C1 and T1,
    // then supports returns true.
    // Or if this set is S1 and T1, and the supplied set contains C1 and T1,
    // then supports returns true.
    //
    // It will do a bitwise and of the linkmode bits. If the result
    // of the and is not zero, then support returns true.
    bool supports(LinkModeSet lms);
    // Check if this set contains the given link mode.
    bool has(LinkMode lm);
    // Check if all link modes are supported.
    bool hasAll(LinkModeSet lms);
    // Check if any link mode has been set.
    bool empty() { return set_ == 0; }
    // Clear the set to empty.
    void clear() { set_ = 0; }
    // Mark set as all linkmodes!
    void setAll() { set_ = (int)LinkMode::Any; }
    // For bit counting etc.
    int asBits() { return set_; }

    // Return a human readable std::string.
    std::string hr();

    LinkModeSet() { }
    LinkModeSet(uint64_t s) : set_(s) {}

private:

    uint64_t set_ {};
};

LinkModeSet parseLinkModes(std::string modes);
bool isValidLinkModes(std::string modes);

enum class CI_TYPE
{
    ELL, NWL, AFL, TPL
};

enum class TPL_LENGTH
{
    NONE, SHORT, LONG
};

#define CC_B_BIDIRECTIONAL_BIT 0x80
#define CC_RD_RESPONSE_DELAY_BIT 0x40
#define CC_S_SYNCH_FRAME_BIT 0x20
#define CC_R_RELAYED_BIT 0x10
#define CC_P_HIGH_PRIO_BIT 0x08

// Bits 31-29 in SN, ie 0xc0 of the final byte in the stream,
// since the bytes arrive with the least significant first
// aka little endian.
#define SN_ENC_BITS 0xc0

#define LIST_OF_ELL_SECURITY_MODES \
    X(NoSecurity, 0) \
    X(AES_CTR, 1) \
    X(RESERVED, 2)

enum class ELLSecurityMode {
#define X(name,nr) name,
LIST_OF_ELL_SECURITY_MODES
#undef X
};

int toInt(ELLSecurityMode esm);
const char *toString(ELLSecurityMode esm);
ELLSecurityMode fromIntToELLSecurityMode(int i);

#define LIST_OF_TPL_SECURITY_MODES \
    X(NoSecurity, 0) \
    X(MFCT_SPECIFIC, 1) \
    X(DES_NO_IV_DEPRECATED, 2) \
    X(DES_IV_DEPRECATED, 3) \
    X(SPECIFIC_4, 4) \
    X(AES_CBC_IV, 5) \
    X(RESERVED_6, 6) \
    X(AES_CBC_NO_IV, 7) \
    X(AES_CTR_CMAC, 8) \
    X(AES_CGM, 9) \
    X(AES_CCM, 10) \
    X(RESERVED_11, 11) \
    X(RESERVED_12, 12) \
    X(SPECIFIC_13, 13) \
    X(RESERVED_14, 14) \
    X(SPECIFIC_15, 15) \
    X(SPECIFIC_16_31, 16)

enum class TPLSecurityMode {
#define X(name,nr) name,
LIST_OF_TPL_SECURITY_MODES
#undef X
};

int toInt(TPLSecurityMode tsm);
TPLSecurityMode fromIntToTPLSecurityMode(int i);
const char *toString(TPLSecurityMode tsm);

#define LIST_OF_AFL_AUTH_TYPES \
    X(NoAuth, 0, 0)             \
    X(Reserved1, 1, 0)          \
    X(Reserved2, 2, 0)          \
    X(AES_CMAC_128_2, 3, 2)     \
    X(AES_CMAC_128_4, 4, 4)     \
    X(AES_CMAC_128_8, 5, 8)     \
    X(AES_CMAC_128_12, 6, 12)   \
    X(AES_CMAC_128_16, 7, 16)   \
    X(AES_GMAC_128_12, 8, 12)

enum class AFLAuthenticationType {
#define X(name,nr,len) name,
LIST_OF_AFL_AUTH_TYPES
#undef X
};

int toInt(AFLAuthenticationType aat);
AFLAuthenticationType fromIntToAFLAuthenticationType(int i);
const char *toString(AFLAuthenticationType aat);
int toLen(AFLAuthenticationType aat);

struct MeterKeys
{
    std::vector<uchar> confidentiality_key;
    std::vector<uchar> authentication_key;

    bool hasConfidentialityKey() { return confidentiality_key.size() > 0; }
    bool hasAuthenticationKey() { return authentication_key.size() > 0; }
};

enum class FrameType
{
    WMBUS,
    MBUS,
    HAN
};

const char *toString(FrameType ft);

struct AboutTelegram
{
    // wmbus device used to receive this telegram.
    std::string device;
    // The device's opinion of the rssi, best effort conversion into the dbm scale.
    // -100 dbm = 0.1 pico Watt to -20 dbm = 10 micro W
    // Measurements smaller than -100 and larger than -10 are unlikely.
    int rssi_dbm {};
    // WMBus or MBus
    FrameType type {};
    // time the telegram was received
    time_t timestamp;

    AboutTelegram(std::string dv, int rs, FrameType t, time_t ts = 0) : device(dv), rssi_dbm(rs), type(t), timestamp(ts) {}
    AboutTelegram() {}
};

// Mark understood bytes as either PROTOCOL, ie dif vif, acc and other header bytes.
// Or CONTENT, ie the value fields found inside the transport layer.
enum class KindOfData
{
    PROTOCOL, CONTENT
};

// Content can be not understood at all NONE, partially understood PARTIAL when typically bitsets have
// been partially decoded, or FULL when the volume or energy field is by itself complete.
// Encrypted if it yet decrypted. Compressed and no format signature is known.
enum class Understanding
{
    NONE, ENCRYPTED, COMPRESSED, PARTIAL, FULL
};

struct Explanation
{
    int pos {};
    int len {};
    std::string info;
    KindOfData kind {};
    Understanding understanding {};

    Explanation(int p, int l, const std::string &i, KindOfData k, Understanding u) :
        pos(p), len(l), info(i), kind(k), understanding(u) {}
};

struct Meter;

struct Telegram
{
private:
    Telegram(Telegram&t) { }

public:
    Telegram() = default;

    AboutTelegram about;

    Meter *meter {};

    // If set to true then this telegram should be trigger updates.
    bool discard {};

    // If a warning is printed mark this.
    bool triggered_warning {};

    // The different addresses found,
    // the first is the dll_id_mvt, ell_id_mvt, nwl_id_mvt, and the last is the tpl_id_mvt.
    std::vector<Address> addresses;

    // If decryption failed, set this to true, to prevent further processing.
    bool decryption_failed {};

    // DLL
    int dll_len {}; // The length of the telegram, 1 byte.
    int dll_c {};   // 1 byte control code, SND_NR=0x44

    uchar dll_mfct_b[2]; //  2 bytes
    int dll_mfct {};

    uchar mbus_primary_address; // Single byte address 0-250 for mbus devices.
    uchar mbus_ci; // MBus control information field.

    std::vector<uchar> dll_a; // A field 6 bytes
    // The 6 a field bytes are composed of 4 id bytes, version and type.
    uchar dll_id_b[4] {};    // 4 bytes, address in BCD = 8 decimal 00000000...99999999 digits.
    std::vector<uchar> dll_id; // 4 bytes, human readable order.
    uchar dll_version {}; // 1 byte
    uchar dll_type {}; // 1 byte

    // ELL
    uchar ell_ci {}; // 1 byte
    uchar ell_cc {}; // 1 byte
    uchar ell_acc {}; // 1 byte
    uchar ell_sn_b[4] {}; // 4 bytes
    int   ell_sn {}; // 4 bytes
    uchar ell_sn_session {}; // 4 bits
    int   ell_sn_time {}; // 25 bits
    uchar ell_sn_sec {}; // 3 bits
    ELLSecurityMode ell_sec_mode {}; // Based on 3 bits from above.
    uchar ell_pl_crc_b[2] {}; // 2 bytes
    uint16_t ell_pl_crc {}; // 2 bytes

    uchar ell_mfct_b[2] {}; // 2 bytes;
    int   ell_mfct {};
    bool  ell_id_found {};
    uchar ell_id_b[6] {}; // 4 bytes;
    uchar ell_version {}; // 1 byte
    uchar ell_type {};  // 1 byte

    // NWL
    int nwl_ci {}; // 1 byte

    // AFL
    uchar afl_ci {}; // 1 byte
    uchar afl_len {}; // 1 byte
    uchar afl_fc_b[2] {}; // 2 byte fragmentation control
    uint16_t afl_fc {};
    uchar afl_mcl {}; // 1 byte message control

    bool afl_ki_found {};
    uchar afl_ki_b[2] {}; // 2 byte key information
    uint16_t afl_ki {};

    bool afl_counter_found {};
    uchar afl_counter_b[4] {}; // 4 bytes
    uint32_t afl_counter {};

    bool afl_mlen_found {};
    int afl_mlen {};

    bool must_check_mac {};
    std::vector<uchar> afl_mac_b;

    // TPL
    std::vector<uchar>::iterator tpl_start;
    int tpl_ci {}; // 1 byte
    int tpl_acc {}; // 1 byte
    int tpl_sts {}; // 1 byte
    int tpl_sts_offset {}; // Remember where the sts field is in the telegram, so
                           // that we can add more vendor specific decodings to it.
    int tpl_cfg {}; // 2 bytes
    TPLSecurityMode tpl_sec_mode {}; // Based on 5 bits extracted from cfg.
    int tpl_num_encr_blocks {};
    int tpl_cfg_ext {}; // 1 byte
    int tpl_kdf_selection {}; // 1 byte
    std::vector<uchar> tpl_generated_key; // 16 bytes
    std::vector<uchar> tpl_generated_mac_key; // 16 bytes

    bool  tpl_id_found {}; // If set to true, then tpl_id_b contains valid values.
    std::vector<uchar> tpl_a; // A field 6 bytes
    // The 6 a field bytes are composed of 4 id bytes, version and type.
    uchar tpl_id_b[4] {}; // 4 bytes
    uchar tpl_mfct_b[2] {}; // 2 bytes
    int   tpl_mfct {};
    uchar tpl_version {}; // 1 bytes
    uchar tpl_type {}; // 1 bytes

    // The format signature is used for compact frames.
    int format_signature {};

    std::vector<uchar> frame; // Content of frame, potentially decrypted.
    std::vector<uchar> parsed;  // Parsed bytes with explanations.
    int header_size {}; // Size of headers before the APL content.
    int suffix_size {}; // Size of suffix after the APL content. Usually empty, but can be MACs!
    int mfct_0f_index = -1; // -1 if not found, else index of the 0f byte, if found, inside the difvif data after the header.
    int mfct_1f_index = -1; // -1 if not found, else index of the 1f byte, if found, then there are more records in the next telegram.
    int force_mfct_index = -1; // Force all data after this offset to be mfct specific. Used for meters not using 0f.
    void extractFrame (std::vector<uchar> *fr); // Extract to full frame.
    void extractPayload (std::vector<uchar> *pl); // Extract frame data containing the measurements, after the header and not the suffix.
    void extractMfctData (std::vector<uchar> *pl); // Extract frame data after the DIF 0x0F.

    bool handled {}; // Set to true, when a meter has accepted the telegram.

    bool parseHeader (std::vector<uchar> &input_frame);
    bool parse (std::vector<uchar> &input_frame, MeterKeys *mk, bool warn);

    bool parseMBUSHeader (std::vector<uchar> &input_frame);
    bool parseMBUS (std::vector<uchar> &input_frame, MeterKeys *mk, bool warn);

    bool parseWMBUSHeader (std::vector<uchar> &input_frame);
    bool parseWMBUS (std::vector<uchar> &input_frame, MeterKeys *mk, bool warn);

    bool parseHANHeader (std::vector<uchar> &input_frame);
    bool parseHAN (std::vector<uchar> &input_frame, MeterKeys *mk, bool warn);

    void addAddressMfctFirst(const std::vector<uchar>::iterator &pos);
    void addAddressIdFirst(const std::vector<uchar>::iterator &pos);

    void print();

    // A std::vector of indentations and explanations, to be printed
    // below the raw data bytes to explain the telegram content.
    std::vector<Explanation> explanations;
    void addExplanationAndIncrementPos (std::vector<uchar>::iterator &pos, int len, KindOfData k, Understanding u, const char* fmt, ...);
    void setExplanation (std::vector<uchar>::iterator &pos, int len, KindOfData k, Understanding u, const char* fmt, ...);
    void addMoreExplanation(int pos, const char* fmt, ...);
    void addMoreExplanation(int pos, std::string json);

    // Add an explanation of data inside manufacturer specific data.
    void addSpecialExplanation(int offset, int len, KindOfData k, Understanding u, const char* fmt, ...);
    void explainParse(std::string intro, int from);
    std::string analyzeParse(OutputFormat o, int *content_length, int *understood_content_length);

    bool parserWarns() { return parser_warns_; }
    bool isSimulated() { return is_simulated_; }
    bool beingAnalyzed() { return being_analyzed_; }
    void markAsSimulated() { is_simulated_ = true; }
    void markAsBeingAnalyzed() { being_analyzed_ = true; }

    // The actual content of the (w)mbus telegram. The DifVif entries.
    // Mapped from their key for quick access to their offset and content.
    std::map<std::string,std::pair<int,DVEntry>> dv_entries;

    std::string autoDetectPossibleDrivers();

    // part of original telegram bytes, only filled if pre-processing modifies it
    std::vector<uchar> original;

private:

    bool is_simulated_ {};
    bool being_analyzed_ {};
    bool parser_warns_ = true;
    MeterKeys *meter_keys {};

    // Fixes quirks from non-compliant meters to make telegram compatible with the standard
    void preProcess();

    bool parseMBusDLLandTPL(std::vector<uchar>::iterator &pos);

    bool parseDLL(std::vector<uchar>::iterator &pos);
    bool parseELL(std::vector<uchar>::iterator &pos);
    bool parseNWL(std::vector<uchar>::iterator &pos);
    bool parseAFL(std::vector<uchar>::iterator &pos);
    bool parseTPL(std::vector<uchar>::iterator &pos);

    void printDLL();
    void printELL();
    void printNWL();
    void printAFL();
    void printTPL();

    bool parse_TPL_72 (std::vector<uchar>::iterator &pos);
    bool parse_TPL_78 (std::vector<uchar>::iterator &pos);
    bool parse_TPL_79 (std::vector<uchar>::iterator &pos);
    bool parse_TPL_7A (std::vector<uchar>::iterator &pos);
    bool alreadyDecryptedCBC (std::vector<uchar>::iterator &pos);
    bool potentiallyDecrypt (std::vector<uchar>::iterator &pos);
    bool parseTPLConfig(std::vector<uchar>::iterator &pos);
    static std::string toStringFromELLSN(int sn);
    static std::string toStringFromTPLConfig(int cfg);
    static std::string toStringFromAFLFC(int fc);
    static std::string toStringFromAFLMC(int mc);

    bool parseShortTPL(std::vector<uchar>::iterator &pos);
    bool parseLongTPL(std::vector<uchar>::iterator &pos);
    bool checkMAC(std::vector<uchar> &frame,
                  std::vector<uchar>::iterator from,
                  std::vector<uchar>::iterator to,
                  std::vector<uchar> &mac,
                  std::vector<uchar> &mackey);
    bool findFormatBytesFromKnownMeterSignatures(std::vector<uchar> *format_bytes);
};

struct SendBusContent
{
    LinkMode link_mode;
    TelegramFormat format;
    std::string bus;
    std::string content;

    static bool isLikely(const std::string &s);
    bool parse(const std::string &s);
};

struct Meter;

std::string manufacturer(int m_field);
std::string mediaType(int a_field_device_type, int m_field);
std::string mediaTypeJSON(int a_field_device_type, int m_field);
bool isCiFieldOfType(int ci_field, CI_TYPE type);
int ciFieldLength(int ci_field);
bool isCiFieldManufacturerSpecific(int ci_field);
std::string ciType(int ci_field);
std::string cType(int c_field);
bool isValidWMBusCField(int c_field);
bool isValidMBusCField(int c_field);
std::string ccType(int cc_field);
std::string difType(int dif);
double vifScale(int vif);
std::string vifKey(int vif); // E.g. temperature energy power mass_flow volume_flow
std::string vifUnit(int vif); // E.g. m3 c kwh kw MJ MJh
std::string vifType(int vif); // Long description
std::string vifeType(int dif, int vif, int vife); // Long description

// Decode only the standard defined bits in the tpl status byte. Ignore the top 3 bits.
// Return "OK" if sts == 0
std::string decodeTPLStatusByteOnlyStandardBits(uchar sts);
// Decode the standard bits and report the top 3 bits if set as for example: UNKNOWN_0x80
// Return "OK" if sts == 0
std::string decodeTPLStatusByteNoMfct(uchar sts);
// Decode the standard bits and translate the top 3 bits if set.
// Return "OK" if sts == 0
std::string decodeTPLStatusByteWithMfct(uchar sts, Translate::Lookup &lookup);

int difLenBytes(int dif);
MeasurementType difMeasurementType(int dif);

std::string linkModeName(LinkMode link_mode);
std::string measurementTypeName(MeasurementType mt);

enum FrameStatus { PartialFrame, FullFrame, ErrorInFrame, TextAndNotFrame };
const char *toString(FrameStatus fs);


FrameStatus checkWMBusFrame (std::vector<uchar> &data,
                            size_t *frame_length,
                            int *payload_len_out,
                            int *payload_offset,
                            bool only_test);

FrameStatus checkMBusFrame (std::vector<uchar> &data,
                           size_t *frame_length,
                           int *payload_len_out,
                           int *payload_offset,
                           bool only_test);

// Remember meters id/mfct/ver/type combos that we should only warn once for.
bool warned_for_telegram_before(Telegram *t, std::vector<uchar> &dll_a);

////////////////// MBUS

const char *mbusCField(uchar c_field);
const char *mbusCiField(uchar ci_field);

int genericifyMedia(int media);
bool isCloseEnough(int media1, int media2);

#endif
