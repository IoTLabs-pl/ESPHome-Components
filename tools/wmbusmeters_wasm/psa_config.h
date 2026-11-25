#pragma once

#define MBEDTLS_PSA_CRYPTO_C
// mbedTLS's own entropy reads /dev/urandom, which -sFILESYSTEM=0 leaves out.
#define MBEDTLS_PSA_CRYPTO_EXTERNAL_RNG

#define PSA_WANT_KEY_TYPE_AES 1
#define PSA_WANT_ALG_ECB_NO_PADDING 1
#define PSA_WANT_ALG_CBC_NO_PADDING 1
