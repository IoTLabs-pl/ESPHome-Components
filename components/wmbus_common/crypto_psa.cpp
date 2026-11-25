#include "crypto.h"

#include <psa/crypto.h>

namespace wmbus {

namespace {

class AesKey {
 public:
  AesKey(std::span<const uint8_t, 16> key, psa_algorithm_t alg, psa_key_usage_t usage) {
    // A no-op once done; IDF 6 does it at startup, IDF 5 and the WASM build do not.
    psa_crypto_init();
    psa_key_attributes_t attributes = PSA_KEY_ATTRIBUTES_INIT;
    psa_set_key_type(&attributes, PSA_KEY_TYPE_AES);
    psa_set_key_bits(&attributes, 128);
    psa_set_key_usage_flags(&attributes, usage);
    psa_set_key_algorithm(&attributes, alg);
    psa_import_key(&attributes, key.data(), key.size(), &this->id_);
  }
  ~AesKey() { psa_destroy_key(this->id_); }
  AesKey(const AesKey &) = delete;
  AesKey &operator=(const AesKey &) = delete;

  psa_key_id_t id() const { return this->id_; }

 private:
  psa_key_id_t id_{PSA_KEY_ID_NULL};
};

}  // namespace

std::array<uint8_t, 16> aes_ecb_encrypt(std::span<const uint8_t, 16> key, std::span<const uint8_t, 16> in) {
  std::array<uint8_t, 16> out;
  const AesKey aes_key(key, PSA_ALG_ECB_NO_PADDING, PSA_KEY_USAGE_ENCRYPT);
  size_t length;
  psa_cipher_encrypt(aes_key.id(), PSA_ALG_ECB_NO_PADDING, in.data(), in.size(), out.data(), out.size(), &length);
  return out;
}

void aes_cbc_decrypt(std::span<const uint8_t, 16> key, const std::array<uint8_t, 16> &iv, std::span<uint8_t> buf) {
  const AesKey aes_key(key, PSA_ALG_CBC_NO_PADDING, PSA_KEY_USAGE_DECRYPT);
  psa_cipher_operation_t op = PSA_CIPHER_OPERATION_INIT;
  if (psa_cipher_decrypt_setup(&op, aes_key.id(), PSA_ALG_CBC_NO_PADDING) == PSA_SUCCESS &&
      psa_cipher_set_iv(&op, iv.data(), iv.size()) == PSA_SUCCESS) {
    // Block by block: IDF builds PSA without in-place support.
    for (size_t i = 0; i + 16 <= buf.size(); i += 16) {
      std::array<uint8_t, 16> block;
      std::copy_n(buf.begin() + i, 16, block.begin());
      size_t length;
      psa_cipher_update(&op, block.data(), block.size(), buf.data() + i, 16, &length);
    }
  }
  psa_cipher_abort(&op);
}

}  // namespace wmbus
