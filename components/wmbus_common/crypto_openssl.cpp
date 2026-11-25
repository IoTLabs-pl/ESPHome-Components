#include "crypto.h"

#include <openssl/evp.h>

namespace wmbus {

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

std::array<uint8_t, 16> aes_ecb_encrypt(std::span<const uint8_t, 16> key, std::span<const uint8_t, 16> in) {
  std::array<uint8_t, 16> out;
  evp(EVP_aes_128_ecb(), key, nullptr, in, out, true);
  return out;
}

void aes_cbc_decrypt(std::span<const uint8_t, 16> key, const std::array<uint8_t, 16> &iv, std::span<uint8_t> buf) {
  buf = buf.first(buf.size() / 16 * 16);
  evp(EVP_aes_128_cbc(), key, iv.data(), buf, buf, false);
}

}  // namespace wmbus
