#include "crypto.h"

#include "esphome/core/defines.h"

#if defined(USE_ESP32) || defined(USE_LIBRETINY)
#define WMBUS_AES_MBEDTLS
#include "mbedtls/aes.h"
#elif defined(USE_HOST)
#define WMBUS_AES_OPENSSL
#include <openssl/evp.h>
#else
#error "wmbus_common needs an AES implementation on this platform"
#endif

namespace wmbus {

#ifdef WMBUS_AES_OPENSSL
namespace {

// OpenSSL 3 exposes AES only through EVP; the per-call context is fine on a host.
void evp(const EVP_CIPHER *cipher, std::span<const uint8_t, 16> key, const uint8_t *iv, std::span<const uint8_t> in,
         std::span<uint8_t> out, bool encrypt) {
  EVP_CIPHER_CTX *ctx = EVP_CIPHER_CTX_new();
  if (ctx == nullptr)
    return;

  if (encrypt)
    EVP_EncryptInit_ex(ctx, cipher, nullptr, key.data(), iv);
  else
    EVP_DecryptInit_ex(ctx, cipher, nullptr, key.data(), iv);
  EVP_CIPHER_CTX_set_padding(ctx, 0);

  int written = 0;
  if (encrypt)
    EVP_EncryptUpdate(ctx, out.data(), &written, in.data(), (int) in.size());
  else
    EVP_DecryptUpdate(ctx, out.data(), &written, in.data(), (int) in.size());

  EVP_CIPHER_CTX_free(ctx);
}

}  // namespace
#endif

std::array<uint8_t, 16> aes_ecb_encrypt(std::span<const uint8_t, 16> key, std::span<const uint8_t, 16> in) {
  std::array<uint8_t, 16> out;
#ifdef WMBUS_AES_MBEDTLS
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key.data(), 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, in.data(), out.data());
  mbedtls_aes_free(&ctx);
#else
  evp(EVP_aes_128_ecb(), key, nullptr, in, out, true);
#endif
  return out;
}

// By value: mbedtls_aes_crypt_cbc advances the iv in place.
void aes_cbc_decrypt(std::span<const uint8_t, 16> key, std::array<uint8_t, 16> iv, std::span<uint8_t> buf) {
  buf = buf.first(buf.size() - buf.size() % 16);
  if (buf.empty())
    return;

#ifdef WMBUS_AES_MBEDTLS
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_dec(&ctx, key.data(), 128);
  mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, buf.size(), iv.data(), buf.data(), buf.data());
  mbedtls_aes_free(&ctx);
#else
  evp(EVP_aes_128_cbc(), key, iv.data(), buf, buf, false);
#endif
}

namespace {

// RFC 4493's subkey step: doubling in GF(2^128).
std::array<uint8_t, 16> doubled(const std::array<uint8_t, 16> &l) {
  std::array<uint8_t, 16> out;
  for (size_t i = 0; i < 15; ++i)
    out[i] = (uint8_t) (l[i] << 1 | l[i + 1] >> 7);
  out[15] = (uint8_t) (l[15] << 1 ^ (l[0] & 0x80 ? 0x87 : 0));
  return out;
}

}  // namespace

std::array<uint8_t, 16> aes_cmac(std::span<const uint8_t, 16> key, std::span<const uint8_t> input) {
  const std::array<uint8_t, 16> k1 = doubled(aes_ecb_encrypt(key, std::array<uint8_t, 16>{}));
  // Where the last block starts; an empty input has an empty one.
  const size_t last = input.empty() ? 0 : (input.size() - 1) / 16 * 16;

  std::array<uint8_t, 16> x{};
  for (size_t i = 0; i < last; i += 16) {
    xor_into(x, input.subspan(i, 16));
    x = aes_ecb_encrypt(key, x);
  }

  const std::span<const uint8_t> tail = input.subspan(last);
  xor_into(x, tail);
  if (tail.size() == 16) {
    xor_into(x, k1);
  } else {
    x[tail.size()] ^= 0x80;
    xor_into(x, doubled(k1));
  }
  return aes_ecb_encrypt(key, x);
}

uint16_t crc16_en13757(std::span<const uint8_t> data) {
  static const uint16_t POLY = 0x3D65;

  uint16_t crc = 0x0000;
  for (uint8_t b : data) {
    for (int bit = 0; bit < 8; ++bit) {
      if (((crc & 0x8000) >> 8) ^ (b & 0x80))
        crc = (uint16_t) ((crc << 1) ^ POLY);
      else
        crc = (uint16_t) (crc << 1);
      b = (uint8_t) (b << 1);
    }
  }
  return (uint16_t) ~crc;
}

}  // namespace wmbus
