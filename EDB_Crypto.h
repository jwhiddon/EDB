/*
  EDB_Crypto.h
  Optional record-level encryption for EDB (opt-in via EDB_ENABLE_CRYPTO).

  v3 crypto model:
    - The EDB core is crypto-agnostic: it stores encrypted records as opaque bytes. All crypto
      lives here (the C reference impl), in the browser, and in the gateway.
    - Real RFC 8439 ChaCha20-Poly1305 AEAD.
    - A fresh 96-bit nonce is stored *with each record* (never derived from its location), so
      re-encrypting an updated record never reuses keystream. Layout of a sealed record:
          [ nonce : 12 ][ ciphertext : plaintext_len ][ tag : 16 ]
      so stored_rec_size = plaintext_len + EDB_CRYPTO_RECORD_OVERHEAD (28).
    - The record's identity (table_id, record_id) is authenticated as AAD, so a ciphertext moved
      to a different table/slot fails verification (no silent swap/replay).

  Define EDB_ENABLE_CRYPTO before including this header. See docs/ENCRYPTION.md and docs/API.md.
*/

#ifndef EDB_CRYPTO_H
#define EDB_CRYPTO_H

#include <stdint.h>
#include <stddef.h>

#ifndef EDB_CRYPTO_TAG_SIZE
#define EDB_CRYPTO_TAG_SIZE 16
#endif

#ifndef EDB_CRYPTO_NONCE_SIZE
#define EDB_CRYPTO_NONCE_SIZE 12
#endif

#ifndef EDB_CRYPTO_KEY_SIZE
#define EDB_CRYPTO_KEY_SIZE 32
#endif

/* Per-record storage overhead: stored nonce + auth tag. */
#define EDB_CRYPTO_RECORD_OVERHEAD (EDB_CRYPTO_NONCE_SIZE + EDB_CRYPTO_TAG_SIZE)

/* Table-level encryption descriptor (metadata written by the host/gateway; opaque to the core). */
#define EDB_CRYPTO_EXT_MAGIC 0xE1
#define EDB_CRYPTO_VERSION_CHACHA20 1
#define EDB_CRYPTO_MODE_BLIND 0
#define EDB_CRYPTO_MODE_AUTONOMOUS 1

#define EDB_CRYPTO_EXT_HEADER_SIZE 24
#define EDB_CRYPTO_WRAPPED_KEY_OFFSET 24
#define EDB_CRYPTO_WRAPPED_KEY_SIZE 48
#define EDB_CRYPTO_EXT_SIZE 72

struct EDB_CryptoExtHeader {
  uint8_t ext_magic;
  uint8_t enc_version;
  uint8_t enc_mode;
  uint8_t reserved;
  uint8_t salt[16];
  uint16_t plaintext_rec_size;
  uint16_t stored_rec_size;
};

#if defined(EDB_ENABLE_CRYPTO)

#ifdef __cplusplus
extern "C" {
#endif

/* --- Low-level AEAD (RFC 8439 ChaCha20-Poly1305) --- */

/* Encrypt plaintext -> ciphertext (same length) and produce a 16-byte tag over aad+ciphertext.
   nonce must be unique per (key). Returns 0 on success, -1 on bad args. */
int edb_crypto_aead_encrypt(const uint8_t key[EDB_CRYPTO_KEY_SIZE],
                            const uint8_t nonce[EDB_CRYPTO_NONCE_SIZE],
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *plaintext, size_t pt_len,
                            uint8_t *ciphertext,
                            uint8_t tag[EDB_CRYPTO_TAG_SIZE]);

/* Verify tag (constant time) then decrypt. Returns 0 on success, -2 on auth failure, -1 bad args. */
int edb_crypto_aead_decrypt(const uint8_t key[EDB_CRYPTO_KEY_SIZE],
                            const uint8_t nonce[EDB_CRYPTO_NONCE_SIZE],
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *ciphertext, size_t ct_len,
                            const uint8_t tag[EDB_CRYPTO_TAG_SIZE],
                            uint8_t *plaintext);

/* --- Record framing --- */

/* Seal a record: out = nonce || ciphertext || tag (out_len = pt_len + 28).
   The caller supplies a fresh unique nonce; identity (table_id, record_id) is bound as AAD. */
int edb_crypto_seal_record(const uint8_t key[EDB_CRYPTO_KEY_SIZE],
                           uint32_t table_id, uint32_t record_id,
                           const uint8_t nonce[EDB_CRYPTO_NONCE_SIZE],
                           const uint8_t *plaintext, size_t pt_len,
                           uint8_t *out, size_t *out_len);

/* Open a sealed record. Returns 0 on success, -2 on auth failure (tamper/swap), -1 bad args. */
int edb_crypto_open_record(const uint8_t key[EDB_CRYPTO_KEY_SIZE],
                           uint32_t table_id, uint32_t record_id,
                           const uint8_t *in, size_t in_len,
                           uint8_t *plaintext, size_t *pt_len);

/* --- Transport session (PSK) --- */

/* Derive a 32-byte session key from a pre-shared key and the two public handshake nonces.
   session_key = ChaCha20(psk, host_nonce, ctr=1)[0..31] XOR ChaCha20(psk, dev_nonce, ctr=1)[0..31]. */
void edb_crypto_session_key(const uint8_t psk[EDB_CRYPTO_KEY_SIZE],
                            const uint8_t host_nonce[EDB_CRYPTO_NONCE_SIZE],
                            const uint8_t dev_nonce[EDB_CRYPTO_NONCE_SIZE],
                            uint8_t out_key[EDB_CRYPTO_KEY_SIZE]);

/* Key-confirmation value proving both sides derived the same session key (16 bytes). */
void edb_crypto_session_confirm(const uint8_t session_key[EDB_CRYPTO_KEY_SIZE],
                                uint8_t out[EDB_CRYPTO_TAG_SIZE]);

/* --- Table descriptor helpers (metadata only) --- */

size_t edb_crypto_parse_ext(const uint8_t *ext_bytes, size_t max_len, EDB_CryptoExtHeader *out);

size_t edb_crypto_write_ext_blind(uint8_t *dest,
                                  const uint8_t salt[16],
                                  uint16_t plaintext_rec_size,
                                  uint16_t stored_rec_size);

#if defined(EDB_CRYPTO_DEVICE_AUTONOMOUS)
size_t edb_crypto_write_ext_autonomous(uint8_t *dest,
                                       const uint8_t salt[16],
                                       uint16_t plaintext_rec_size,
                                       uint16_t stored_rec_size,
                                       const uint8_t wrapped_key[48]);
#endif

#ifdef __cplusplus
}
#endif

#endif /* EDB_ENABLE_CRYPTO */

#endif /* EDB_CRYPTO_H */
