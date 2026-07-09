#pragma once

#include <stdint.h>
#include <string.h>

// Crypto is enabled by the build (-DEDB_ENABLE_CRYPTO), not forced here, so the same test
// sources also compile in the crypto-less 1.0.7 drop-in build (where the bodies are no-ops).
#include "../../EDB_Crypto.h"

#if defined(EDB_ENABLE_CRYPTO)
inline void cryptoTestKey(uint8_t key[EDB_CRYPTO_KEY_SIZE]) {
  memset(key, 0x42, EDB_CRYPTO_KEY_SIZE);
}
#endif
