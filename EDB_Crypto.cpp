/*
  EDB_Crypto.cpp — see EDB_Crypto.h

  RFC 8439 ChaCha20-Poly1305 AEAD. Poly1305 is the 32-bit "donna" implementation (public-domain,
  by Andrew Moon), chosen for portability to 8/32-bit MCUs. Validated against the RFC 8439 §2.8.2
  test vector in test/test_crypto.cpp.
*/
#include "EDB_Crypto.h"

#if defined(EDB_ENABLE_CRYPTO)

#include <string.h>

/* ------------------------------------------------------------------ ChaCha20 */

static void edb_chacha_quarterround(uint32_t *a, uint32_t *b, uint32_t *c, uint32_t *d) {
  *a += *b; *d ^= *a; *d = (*d << 16) | (*d >> 16);
  *c += *d; *b ^= *c; *b = (*b << 12) | (*b >> 20);
  *a += *b; *d ^= *a; *d = (*d << 8) | (*d >> 24);
  *c += *d; *b ^= *c; *b = (*b << 7) | (*b >> 25);
}

static void edb_chacha20_block(const uint8_t key[32], const uint8_t nonce[12],
                               uint32_t counter, uint8_t out[64]) {
  static const char constants[16] =
      {'e','x','p','a','n','d',' ','3','2','-','b','y','t','e',' ','k'};
  uint32_t state[16];
  uint32_t working[16];
  int i;
  for (i = 0; i < 4; i++)
    state[i] = ((uint32_t)(uint8_t)constants[4 * i]) | ((uint32_t)(uint8_t)constants[4 * i + 1] << 8) |
        ((uint32_t)(uint8_t)constants[4 * i + 2] << 16) | ((uint32_t)(uint8_t)constants[4 * i + 3] << 24);
  for (i = 0; i < 8; i++)
    state[4 + i] = ((uint32_t)key[4 * i]) | ((uint32_t)key[4 * i + 1] << 8) |
                   ((uint32_t)key[4 * i + 2] << 16) | ((uint32_t)key[4 * i + 3] << 24);
  state[12] = counter;
  state[13] = ((uint32_t)nonce[0]) | ((uint32_t)nonce[1] << 8) | ((uint32_t)nonce[2] << 16) | ((uint32_t)nonce[3] << 24);
  state[14] = ((uint32_t)nonce[4]) | ((uint32_t)nonce[5] << 8) | ((uint32_t)nonce[6] << 16) | ((uint32_t)nonce[7] << 24);
  state[15] = ((uint32_t)nonce[8]) | ((uint32_t)nonce[9] << 8) | ((uint32_t)nonce[10] << 16) | ((uint32_t)nonce[11] << 24);
  memcpy(working, state, sizeof(state));
  for (i = 0; i < 10; i++) {
    edb_chacha_quarterround(&working[0], &working[4], &working[8], &working[12]);
    edb_chacha_quarterround(&working[1], &working[5], &working[9], &working[13]);
    edb_chacha_quarterround(&working[2], &working[6], &working[10], &working[14]);
    edb_chacha_quarterround(&working[3], &working[7], &working[11], &working[15]);
    edb_chacha_quarterround(&working[0], &working[5], &working[10], &working[15]);
    edb_chacha_quarterround(&working[1], &working[6], &working[11], &working[12]);
    edb_chacha_quarterround(&working[2], &working[7], &working[8], &working[13]);
    edb_chacha_quarterround(&working[3], &working[4], &working[9], &working[14]);
  }
  for (i = 0; i < 16; i++) {
    uint32_t w = working[i] + state[i];
    out[4 * i]     = (uint8_t)w;
    out[4 * i + 1] = (uint8_t)(w >> 8);
    out[4 * i + 2] = (uint8_t)(w >> 16);
    out[4 * i + 3] = (uint8_t)(w >> 24);
  }
}

/* XOR ChaCha20 keystream (starting at block `counter`) into in -> out. */
static void edb_chacha20_xor(const uint8_t key[32], const uint8_t nonce[12], uint32_t counter,
                             const uint8_t *in, size_t len, uint8_t *out) {
  uint8_t block[64];
  size_t i = 0;
  while (i < len) {
    edb_chacha20_block(key, nonce, counter, block);
    size_t n = len - i;
    if (n > 64) n = 64;
    for (size_t j = 0; j < n; j++) out[i + j] = in[i + j] ^ block[j];
    i += n;
    counter++;
  }
}

/* ------------------------------------------------------------------ Poly1305 (donna 32-bit) */

typedef struct {
  uint32_t r[5];
  uint32_t h[5];
  uint32_t pad[4];
  size_t leftover;
  uint8_t buffer[16];
  uint8_t final;
} edb_poly1305_ctx;

static uint32_t edb_u8to32(const uint8_t *p) {
  return ((uint32_t)p[0]) | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static void edb_u32to8(uint8_t *p, uint32_t v) {
  p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24);
}

static void edb_poly1305_init(edb_poly1305_ctx *st, const uint8_t key[32]) {
  st->r[0] = (edb_u8to32(&key[0])) & 0x3ffffff;
  st->r[1] = (edb_u8to32(&key[3]) >> 2) & 0x3ffff03;
  st->r[2] = (edb_u8to32(&key[6]) >> 4) & 0x3ffc0ff;
  st->r[3] = (edb_u8to32(&key[9]) >> 6) & 0x3f03fff;
  st->r[4] = (edb_u8to32(&key[12]) >> 8) & 0x00fffff;
  st->h[0] = st->h[1] = st->h[2] = st->h[3] = st->h[4] = 0;
  st->pad[0] = edb_u8to32(&key[16]);
  st->pad[1] = edb_u8to32(&key[20]);
  st->pad[2] = edb_u8to32(&key[24]);
  st->pad[3] = edb_u8to32(&key[28]);
  st->leftover = 0;
  st->final = 0;
}

static void edb_poly1305_blocks(edb_poly1305_ctx *st, const uint8_t *m, size_t bytes) {
  const uint32_t hibit = st->final ? 0 : (1UL << 24);
  uint32_t r0 = st->r[0], r1 = st->r[1], r2 = st->r[2], r3 = st->r[3], r4 = st->r[4];
  uint32_t s1 = r1 * 5, s2 = r2 * 5, s3 = r3 * 5, s4 = r4 * 5;
  uint32_t h0 = st->h[0], h1 = st->h[1], h2 = st->h[2], h3 = st->h[3], h4 = st->h[4];
  while (bytes >= 16) {
    uint64_t d0, d1, d2, d3, d4;
    uint32_t c;
    h0 += (edb_u8to32(m + 0)) & 0x3ffffff;
    h1 += (edb_u8to32(m + 3) >> 2) & 0x3ffffff;
    h2 += (edb_u8to32(m + 6) >> 4) & 0x3ffffff;
    h3 += (edb_u8to32(m + 9) >> 6) & 0x3ffffff;
    h4 += (edb_u8to32(m + 12) >> 8) | hibit;
    d0 = (uint64_t)h0 * r0 + (uint64_t)h1 * s4 + (uint64_t)h2 * s3 + (uint64_t)h3 * s2 + (uint64_t)h4 * s1;
    d1 = (uint64_t)h0 * r1 + (uint64_t)h1 * r0 + (uint64_t)h2 * s4 + (uint64_t)h3 * s3 + (uint64_t)h4 * s2;
    d2 = (uint64_t)h0 * r2 + (uint64_t)h1 * r1 + (uint64_t)h2 * r0 + (uint64_t)h3 * s4 + (uint64_t)h4 * s3;
    d3 = (uint64_t)h0 * r3 + (uint64_t)h1 * r2 + (uint64_t)h2 * r1 + (uint64_t)h3 * r0 + (uint64_t)h4 * s4;
    d4 = (uint64_t)h0 * r4 + (uint64_t)h1 * r3 + (uint64_t)h2 * r2 + (uint64_t)h3 * r1 + (uint64_t)h4 * r0;
    c = (uint32_t)(d0 >> 26); h0 = (uint32_t)d0 & 0x3ffffff;
    d1 += c; c = (uint32_t)(d1 >> 26); h1 = (uint32_t)d1 & 0x3ffffff;
    d2 += c; c = (uint32_t)(d2 >> 26); h2 = (uint32_t)d2 & 0x3ffffff;
    d3 += c; c = (uint32_t)(d3 >> 26); h3 = (uint32_t)d3 & 0x3ffffff;
    d4 += c; c = (uint32_t)(d4 >> 26); h4 = (uint32_t)d4 & 0x3ffffff;
    h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
    m += 16; bytes -= 16;
  }
  st->h[0] = h0; st->h[1] = h1; st->h[2] = h2; st->h[3] = h3; st->h[4] = h4;
}

static void edb_poly1305_update(edb_poly1305_ctx *st, const uint8_t *m, size_t bytes) {
  size_t i;
  if (st->leftover) {
    size_t want = 16 - st->leftover;
    if (want > bytes) want = bytes;
    for (i = 0; i < want; i++) st->buffer[st->leftover + i] = m[i];
    bytes -= want; m += want; st->leftover += want;
    if (st->leftover < 16) return;
    edb_poly1305_blocks(st, st->buffer, 16);
    st->leftover = 0;
  }
  if (bytes >= 16) {
    size_t want = bytes & ~(size_t)15;
    edb_poly1305_blocks(st, m, want);
    m += want; bytes -= want;
  }
  for (i = 0; i < bytes; i++) st->buffer[st->leftover + i] = m[i];
  st->leftover += bytes;
}

static void edb_poly1305_finish(edb_poly1305_ctx *st, uint8_t mac[16]) {
  uint32_t h0, h1, h2, h3, h4, c;
  uint32_t g0, g1, g2, g3, g4;
  uint64_t f;
  uint32_t mask;
  if (st->leftover) {
    size_t i = st->leftover;
    st->buffer[i++] = 1;
    for (; i < 16; i++) st->buffer[i] = 0;
    st->final = 1;
    edb_poly1305_blocks(st, st->buffer, 16);
  }
  h0 = st->h[0]; h1 = st->h[1]; h2 = st->h[2]; h3 = st->h[3]; h4 = st->h[4];
  c = h1 >> 26; h1 &= 0x3ffffff;
  h2 += c; c = h2 >> 26; h2 &= 0x3ffffff;
  h3 += c; c = h3 >> 26; h3 &= 0x3ffffff;
  h4 += c; c = h4 >> 26; h4 &= 0x3ffffff;
  h0 += c * 5; c = h0 >> 26; h0 &= 0x3ffffff; h1 += c;
  g0 = h0 + 5; c = g0 >> 26; g0 &= 0x3ffffff;
  g1 = h1 + c; c = g1 >> 26; g1 &= 0x3ffffff;
  g2 = h2 + c; c = g2 >> 26; g2 &= 0x3ffffff;
  g3 = h3 + c; c = g3 >> 26; g3 &= 0x3ffffff;
  g4 = h4 + c - (1UL << 26);
  mask = (g4 >> 31) - 1;
  g0 &= mask; g1 &= mask; g2 &= mask; g3 &= mask; g4 &= mask;
  mask = ~mask;
  h0 = (h0 & mask) | g0; h1 = (h1 & mask) | g1; h2 = (h2 & mask) | g2;
  h3 = (h3 & mask) | g3; h4 = (h4 & mask) | g4;
  h0 = ((h0) | (h1 << 26)) & 0xffffffff;
  h1 = ((h1 >> 6) | (h2 << 20)) & 0xffffffff;
  h2 = ((h2 >> 12) | (h3 << 14)) & 0xffffffff;
  h3 = ((h3 >> 18) | (h4 << 8)) & 0xffffffff;
  f = (uint64_t)h0 + st->pad[0]; h0 = (uint32_t)f;
  f = (uint64_t)h1 + st->pad[1] + (f >> 32); h1 = (uint32_t)f;
  f = (uint64_t)h2 + st->pad[2] + (f >> 32); h2 = (uint32_t)f;
  f = (uint64_t)h3 + st->pad[3] + (f >> 32); h3 = (uint32_t)f;
  edb_u32to8(mac + 0, h0); edb_u32to8(mac + 4, h1);
  edb_u32to8(mac + 8, h2); edb_u32to8(mac + 12, h3);
}

static void edb_poly1305_pad16(edb_poly1305_ctx *st, size_t len) {
  static const uint8_t zero[16] = {0};
  size_t rem = len % 16;
  if (rem) edb_poly1305_update(st, zero, 16 - rem);
}

/* ------------------------------------------------------------------ AEAD */

static void edb_le64(uint8_t *p, uint64_t v) {
  for (int i = 0; i < 8; i++) p[i] = (uint8_t)(v >> (8 * i));
}

/* Constant-time equality: returns 0 if equal, nonzero otherwise. */
static int edb_ct_neq(const uint8_t *a, const uint8_t *b, size_t n) {
  uint8_t r = 0;
  for (size_t i = 0; i < n; i++) r |= (uint8_t)(a[i] ^ b[i]);
  return r;
}

static void edb_poly1305_tag(const uint8_t otk[32], const uint8_t *aad, size_t aad_len,
                             const uint8_t *ct, size_t ct_len, uint8_t tag[16]) {
  edb_poly1305_ctx st;
  uint8_t lenblock[16];
  edb_poly1305_init(&st, otk);
  if (aad_len) { edb_poly1305_update(&st, aad, aad_len); edb_poly1305_pad16(&st, aad_len); }
  if (ct_len) { edb_poly1305_update(&st, ct, ct_len); edb_poly1305_pad16(&st, ct_len); }
  edb_le64(lenblock, (uint64_t)aad_len);
  edb_le64(lenblock + 8, (uint64_t)ct_len);
  edb_poly1305_update(&st, lenblock, 16);
  edb_poly1305_finish(&st, tag);
}

int edb_crypto_aead_encrypt(const uint8_t key[EDB_CRYPTO_KEY_SIZE],
                            const uint8_t nonce[EDB_CRYPTO_NONCE_SIZE],
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *plaintext, size_t pt_len,
                            uint8_t *ciphertext,
                            uint8_t tag[EDB_CRYPTO_TAG_SIZE]) {
  uint8_t block0[64];
  uint8_t otk[32];
  if (!key || !nonce || !tag) return -1;
  if (pt_len && (!plaintext || !ciphertext)) return -1;
  if (aad_len && !aad) return -1;
  edb_chacha20_block(key, nonce, 0, block0);
  memcpy(otk, block0, 32);
  edb_chacha20_xor(key, nonce, 1, plaintext, pt_len, ciphertext);
  edb_poly1305_tag(otk, aad, aad_len, ciphertext, pt_len, tag);
  return 0;
}

int edb_crypto_aead_decrypt(const uint8_t key[EDB_CRYPTO_KEY_SIZE],
                            const uint8_t nonce[EDB_CRYPTO_NONCE_SIZE],
                            const uint8_t *aad, size_t aad_len,
                            const uint8_t *ciphertext, size_t ct_len,
                            const uint8_t tag[EDB_CRYPTO_TAG_SIZE],
                            uint8_t *plaintext) {
  uint8_t block0[64];
  uint8_t otk[32];
  uint8_t expected[16];
  if (!key || !nonce || !tag) return -1;
  if (ct_len && (!ciphertext || !plaintext)) return -1;
  if (aad_len && !aad) return -1;
  edb_chacha20_block(key, nonce, 0, block0);
  memcpy(otk, block0, 32);
  edb_poly1305_tag(otk, aad, aad_len, ciphertext, ct_len, expected);
  if (edb_ct_neq(expected, tag, EDB_CRYPTO_TAG_SIZE) != 0) return -2;
  edb_chacha20_xor(key, nonce, 1, ciphertext, ct_len, plaintext);
  return 0;
}

/* ------------------------------------------------------------------ record framing */

static void edb_crypto_record_aad(uint32_t table_id, uint32_t record_id, uint8_t aad[8]) {
  aad[0] = (uint8_t)table_id;  aad[1] = (uint8_t)(table_id >> 8);
  aad[2] = (uint8_t)(table_id >> 16); aad[3] = (uint8_t)(table_id >> 24);
  aad[4] = (uint8_t)record_id; aad[5] = (uint8_t)(record_id >> 8);
  aad[6] = (uint8_t)(record_id >> 16); aad[7] = (uint8_t)(record_id >> 24);
}

int edb_crypto_seal_record(const uint8_t key[EDB_CRYPTO_KEY_SIZE],
                           uint32_t table_id, uint32_t record_id,
                           const uint8_t nonce[EDB_CRYPTO_NONCE_SIZE],
                           const uint8_t *plaintext, size_t pt_len,
                           uint8_t *out, size_t *out_len) {
  uint8_t aad[8];
  if (!out || !out_len || !nonce || (pt_len && !plaintext)) return -1;
  edb_crypto_record_aad(table_id, record_id, aad);
  memcpy(out, nonce, EDB_CRYPTO_NONCE_SIZE);
  int rc = edb_crypto_aead_encrypt(key, nonce, aad, sizeof(aad), plaintext, pt_len,
                                   out + EDB_CRYPTO_NONCE_SIZE,
                                   out + EDB_CRYPTO_NONCE_SIZE + pt_len);
  if (rc != 0) return rc;
  *out_len = pt_len + EDB_CRYPTO_RECORD_OVERHEAD;
  return 0;
}

int edb_crypto_open_record(const uint8_t key[EDB_CRYPTO_KEY_SIZE],
                           uint32_t table_id, uint32_t record_id,
                           const uint8_t *in, size_t in_len,
                           uint8_t *plaintext, size_t *pt_len) {
  uint8_t aad[8];
  if (!in || !pt_len || in_len < EDB_CRYPTO_RECORD_OVERHEAD) return -1;
  size_t ct_len = in_len - EDB_CRYPTO_RECORD_OVERHEAD;
  if (ct_len && !plaintext) return -1;
  edb_crypto_record_aad(table_id, record_id, aad);
  int rc = edb_crypto_aead_decrypt(key, in, aad, sizeof(aad),
                                   in + EDB_CRYPTO_NONCE_SIZE, ct_len,
                                   in + EDB_CRYPTO_NONCE_SIZE + ct_len, plaintext);
  if (rc != 0) return rc;
  *pt_len = ct_len;
  return 0;
}

/* ------------------------------------------------------------------ table descriptor helpers */

size_t edb_crypto_parse_ext(const uint8_t *ext_bytes, size_t max_len, EDB_CryptoExtHeader *out) {
  if (!ext_bytes || !out || max_len < EDB_CRYPTO_EXT_HEADER_SIZE) return 0;
  if (ext_bytes[0] != EDB_CRYPTO_EXT_MAGIC) return 0;
  out->ext_magic = ext_bytes[0];
  out->enc_version = ext_bytes[1];
  out->enc_mode = ext_bytes[2];
  out->reserved = ext_bytes[3];
  memcpy(out->salt, ext_bytes + 4, 16);
  out->plaintext_rec_size = (uint16_t)(ext_bytes[20] | (ext_bytes[21] << 8));
  out->stored_rec_size = (uint16_t)(ext_bytes[22] | (ext_bytes[23] << 8));
  if (max_len < EDB_CRYPTO_EXT_SIZE) return 0;
  return EDB_CRYPTO_EXT_SIZE;
}

size_t edb_crypto_write_ext_blind(uint8_t *dest,
                                  const uint8_t salt[16],
                                  uint16_t plaintext_rec_size,
                                  uint16_t stored_rec_size) {
  dest[0] = EDB_CRYPTO_EXT_MAGIC;
  dest[1] = EDB_CRYPTO_VERSION_CHACHA20;
  dest[2] = EDB_CRYPTO_MODE_BLIND;
  dest[3] = 0;
  memcpy(dest + 4, salt, 16);
  dest[20] = (uint8_t)(plaintext_rec_size);
  dest[21] = (uint8_t)(plaintext_rec_size >> 8);
  dest[22] = (uint8_t)(stored_rec_size);
  dest[23] = (uint8_t)(stored_rec_size >> 8);
  memset(dest + EDB_CRYPTO_WRAPPED_KEY_OFFSET, 0, EDB_CRYPTO_WRAPPED_KEY_SIZE);
  return EDB_CRYPTO_EXT_SIZE;
}

#if defined(EDB_CRYPTO_DEVICE_AUTONOMOUS)
size_t edb_crypto_write_ext_autonomous(uint8_t *dest,
                                       const uint8_t salt[16],
                                       uint16_t plaintext_rec_size,
                                       uint16_t stored_rec_size,
                                       const uint8_t wrapped_key[48]) {
  edb_crypto_write_ext_blind(dest, salt, plaintext_rec_size, stored_rec_size);
  dest[2] = EDB_CRYPTO_MODE_AUTONOMOUS;
  memcpy(dest + EDB_CRYPTO_WRAPPED_KEY_OFFSET, wrapped_key, EDB_CRYPTO_WRAPPED_KEY_SIZE);
  return EDB_CRYPTO_EXT_SIZE;
}
#endif

#endif /* EDB_ENABLE_CRYPTO */
