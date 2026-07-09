/*
  bridge_session.h — PSK transport session framing for the EDB Serial Bridge.

  Pure functions (no Serial/SD dependencies) so the crypto framing can be unit-tested on a host.
  Requires EDB_Crypto (define EDB_ENABLE_CRYPTO and link EDB_Crypto.cpp). The wire framing matches
  the gateway's EncryptedTransport:
      frame = nonce(12) || ciphertext || tag(16)
      nonce = direction(1) || counter(LE64) || 0x000000
  Direction 0x01 = host->device, 0x02 = device->host.
*/
#ifndef EDB_BRIDGE_SESSION_H
#define EDB_BRIDGE_SESSION_H

#include <stdint.h>
#include <string.h>
#include "EDB_Crypto.h"

#define EDB_BRIDGE_DIR_HOST 0x01
#define EDB_BRIDGE_DIR_DEVICE 0x02

static inline void bridge_session_nonce(uint8_t dir, uint32_t counter, uint8_t nonce[12]) {
  memset(nonce, 0, 12);
  nonce[0] = dir;
  nonce[1] = (uint8_t)(counter);
  nonce[2] = (uint8_t)(counter >> 8);
  nonce[3] = (uint8_t)(counter >> 16);
  nonce[4] = (uint8_t)(counter >> 24);
}

/* Seal plaintext into out = nonce||ciphertext||tag. out must hold 12 + pt_len + 16 bytes.
   Returns the framed length. */
static inline size_t bridge_session_seal(const uint8_t session_key[32], uint8_t dir, uint32_t counter,
                                         const uint8_t *pt, size_t pt_len, uint8_t *out) {
  uint8_t nonce[12];
  bridge_session_nonce(dir, counter, nonce);
  memcpy(out, nonce, 12);
  edb_crypto_aead_encrypt(session_key, nonce, 0, 0, pt, pt_len, out + 12, out + 12 + pt_len);
  return 12 + pt_len + 16;
}

/* Open a framed line. Verifies the direction and authenticates before decrypting.
   Writes plaintext to out (>= framed_len). Returns plaintext length, or -1 on failure. */
static inline int bridge_session_open(const uint8_t session_key[32], uint8_t expect_dir,
                                      const uint8_t *framed, size_t framed_len,
                                      uint8_t *out, uint32_t *out_counter) {
  if (framed_len < 12u + EDB_CRYPTO_TAG_SIZE) return -1;
  if (framed[0] != expect_dir) return -1;
  size_t ct_len = framed_len - 12 - EDB_CRYPTO_TAG_SIZE;
  int rc = edb_crypto_aead_decrypt(session_key, framed, 0, 0,
                                   framed + 12, ct_len, framed + 12 + ct_len, out);
  if (rc != 0) return -1;
  if (out_counter) {
    *out_counter = (uint32_t)framed[1] | ((uint32_t)framed[2] << 8) |
                   ((uint32_t)framed[3] << 16) | ((uint32_t)framed[4] << 24);
  }
  return (int)ct_len;
}

#endif /* EDB_BRIDGE_SESSION_H */
