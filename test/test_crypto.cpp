#include "unity.h"
#include "CryptoTestHelpers.h"

#if defined(EDB_ENABLE_CRYPTO)

#include <string.h>

// RFC 8439 section 2.8.2 AEAD ChaCha20-Poly1305 known-answer test.
void test_aead_rfc8439_vector() {
  static const uint8_t key[32] = {
    0x80,0x81,0x82,0x83,0x84,0x85,0x86,0x87,0x88,0x89,0x8a,0x8b,0x8c,0x8d,0x8e,0x8f,
    0x90,0x91,0x92,0x93,0x94,0x95,0x96,0x97,0x98,0x99,0x9a,0x9b,0x9c,0x9d,0x9e,0x9f };
  static const uint8_t nonce[12] = {0x07,0x00,0x00,0x00,0x40,0x41,0x42,0x43,0x44,0x45,0x46,0x47};
  static const uint8_t aad[12] = {0x50,0x51,0x52,0x53,0xc0,0xc1,0xc2,0xc3,0xc4,0xc5,0xc6,0xc7};
  static const char *pt =
    "Ladies and Gentlemen of the class of '99: If I could offer you only one tip "
    "for the future, sunscreen would be it.";
  const size_t pt_len = 114;
  static const uint8_t expect_ct[114] = {
    0xd3, 0x1a, 0x8d, 0x34, 0x64, 0x8e, 0x60, 0xdb, 0x7b, 0x86, 0xaf, 0xbc, 0x53, 0xef, 0x7e, 0xc2,
    0xa4, 0xad, 0xed, 0x51, 0x29, 0x6e, 0x08, 0xfe, 0xa9, 0xe2, 0xb5, 0xa7, 0x36, 0xee, 0x62, 0xd6,
    0x3d, 0xbe, 0xa4, 0x5e, 0x8c, 0xa9, 0x67, 0x12, 0x82, 0xfa, 0xfb, 0x69, 0xda, 0x92, 0x72, 0x8b,
    0x1a, 0x71, 0xde, 0x0a, 0x9e, 0x06, 0x0b, 0x29, 0x05, 0xd6, 0xa5, 0xb6, 0x7e, 0xcd, 0x3b, 0x36,
    0x92, 0xdd, 0xbd, 0x7f, 0x2d, 0x77, 0x8b, 0x8c, 0x98, 0x03, 0xae, 0xe3, 0x28, 0x09, 0x1b, 0x58,
    0xfa, 0xb3, 0x24, 0xe4, 0xfa, 0xd6, 0x75, 0x94, 0x55, 0x85, 0x80, 0x8b, 0x48, 0x31, 0xd7, 0xbc,
    0x3f, 0xf4, 0xde, 0xf0, 0x8e, 0x4b, 0x7a, 0x9d, 0xe5, 0x76, 0xd2, 0x65, 0x86, 0xce, 0xc6, 0x4b,
    0x61, 0x16 };
  static const uint8_t expect_tag[16] = {
    0x1a,0xe1,0x0b,0x59,0x4f,0x09,0xe2,0x6a,0x7e,0x90,0x2e,0xcb,0xd0,0x60,0x06,0x91 };

  uint8_t ct[114];
  uint8_t tag[16];
  TEST_ASSERT_EQUAL_INT(0, edb_crypto_aead_encrypt(key, nonce, aad, sizeof(aad),
                                                   (const uint8_t*)pt, pt_len, ct, tag));
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expect_ct, ct, pt_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expect_tag, tag, 16);

  uint8_t out[114];
  TEST_ASSERT_EQUAL_INT(0, edb_crypto_aead_decrypt(key, nonce, aad, sizeof(aad),
                                                   ct, pt_len, tag, out));
  TEST_ASSERT_EQUAL_UINT8_ARRAY((const uint8_t*)pt, out, pt_len);
}

void test_aead_tamper_detected() {
  uint8_t key[32]; cryptoTestKey(key);
  uint8_t nonce[12] = {1,2,3,4,5,6,7,8,9,10,11,12};
  uint8_t pt[8] = {1,2,3,4,5,6,7,8};
  uint8_t ct[8]; uint8_t tag[16]; uint8_t out[8];
  edb_crypto_aead_encrypt(key, nonce, NULL, 0, pt, 8, ct, tag);
  ct[3] ^= 0x01;   // flip a ciphertext bit
  TEST_ASSERT_EQUAL_INT(-2, edb_crypto_aead_decrypt(key, nonce, NULL, 0, ct, 8, tag, out));
}

void test_seal_open_roundtrip() {
  uint8_t key[32]; cryptoTestKey(key);
  uint8_t nonce[12] = {9,9,9,9,0,0,0,0,1,2,3,4};
  uint8_t pt[4] = {0xDE,0xAD,0xBE,0xEF};
  uint8_t blob[64]; size_t blob_len = 0;
  TEST_ASSERT_EQUAL_INT(0, edb_crypto_seal_record(key, 7, 42, nonce, pt, 4, blob, &blob_len));
  TEST_ASSERT_EQUAL_UINT32(4 + EDB_CRYPTO_RECORD_OVERHEAD, (unsigned)blob_len);
  uint8_t out[4]; size_t out_len = 0;
  TEST_ASSERT_EQUAL_INT(0, edb_crypto_open_record(key, 7, 42, blob, blob_len, out, &out_len));
  TEST_ASSERT_EQUAL_UINT32(4, (unsigned)out_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(pt, out, 4);
}

// Moving a ciphertext to a different record identity must fail (AAD binds table_id, record_id).
void test_seal_swap_rejected() {
  uint8_t key[32]; cryptoTestKey(key);
  uint8_t nonce[12] = {5,5,5,5,5,5,5,5,5,5,5,5};
  uint8_t pt[4] = {1,2,3,4};
  uint8_t blob[64]; size_t blob_len = 0;
  edb_crypto_seal_record(key, 1, 1, nonce, pt, 4, blob, &blob_len);
  uint8_t out[4]; size_t out_len = 0;
  TEST_ASSERT_EQUAL_INT(0,  edb_crypto_open_record(key, 1, 1, blob, blob_len, out, &out_len));
  TEST_ASSERT_EQUAL_INT(-2, edb_crypto_open_record(key, 1, 2, blob, blob_len, out, &out_len)); // wrong record
  TEST_ASSERT_EQUAL_INT(-2, edb_crypto_open_record(key, 2, 1, blob, blob_len, out, &out_len)); // wrong table
}

// Distinct per-record nonces must yield distinct keystream (no two-time pad on re-encrypt).
void test_nonce_uniqueness_changes_ciphertext() {
  uint8_t key[32]; cryptoTestKey(key);
  uint8_t n1[12] = {0}; uint8_t n2[12] = {0}; n2[0] = 1;
  uint8_t pt[4] = {9,9,9,9};
  uint8_t a[64]; uint8_t b[64]; size_t la = 0, lb = 0;
  edb_crypto_seal_record(key, 1, 1, n1, pt, 4, a, &la);
  edb_crypto_seal_record(key, 1, 1, n2, pt, 4, b, &lb);
  // ciphertext bytes (after the 12-byte stored nonce) must differ
  TEST_ASSERT_NOT_EQUAL(0, memcmp(a + EDB_CRYPTO_NONCE_SIZE, b + EDB_CRYPTO_NONCE_SIZE, 4));
}

// Cross-impl known-answer: must match test/fixtures/crypto_vectors.json (sealed_hex), which is
// generated authoritatively (OpenSSL/RFC 8439). Any other implementation checking the same file
// is thereby cross-validated against this one.
void test_seal_record_shared_kat() {
  uint8_t key[32]; memset(key, 0x42, sizeof(key));
  uint8_t nonce[12] = {0,1,2,3,4,5,6,7,8,9,10,11};
  uint8_t pt[4] = {1,2,3,4};
  static const uint8_t expect_sealed[32] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,   // nonce
    0xe5,0x42,0x57,0x42,                                           // ciphertext
    0x57,0xef,0x18,0x7d,0x48,0x6b,0xd3,0x59,0x24,0x53,0x4d,0xa5,0xd4,0xeb,0x93,0xaa }; // tag
  uint8_t blob[32]; size_t blob_len = 0;
  TEST_ASSERT_EQUAL_INT(0, edb_crypto_seal_record(key, 7, 42, nonce, pt, 4, blob, &blob_len));
  TEST_ASSERT_EQUAL_UINT32(32, (unsigned)blob_len);
  TEST_ASSERT_EQUAL_UINT8_ARRAY(expect_sealed, blob, 32);
}

int run_crypto_tests() {
  UNITY_BEGIN();
  RUN_TEST(test_aead_rfc8439_vector);
  RUN_TEST(test_aead_tamper_detected);
  RUN_TEST(test_seal_open_roundtrip);
  RUN_TEST(test_seal_swap_rejected);
  RUN_TEST(test_nonce_uniqueness_changes_ciphertext);
  RUN_TEST(test_seal_record_shared_kat);
  return UNITY_END();
}
#else
int run_crypto_tests() { return 0; }
#endif
