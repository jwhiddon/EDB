#include "unity.h"
#include "CryptoTestHelpers.h"

#if defined(EDB_ENABLE_CRYPTO)

void test_crypto_ext_blind_parse() {
  uint8_t buf[EDB_CRYPTO_EXT_SIZE];
  uint8_t salt[16] = {0};
  edb_crypto_write_ext_blind(buf, salt, 8, 36);
  EDB_CryptoExtHeader hdr;
  size_t n = edb_crypto_parse_ext(buf, EDB_CRYPTO_EXT_SIZE, &hdr);
  TEST_ASSERT_EQUAL_UINT32(EDB_CRYPTO_EXT_SIZE, (unsigned)n);
  TEST_ASSERT_EQUAL_UINT8(EDB_CRYPTO_EXT_MAGIC, hdr.ext_magic);
  TEST_ASSERT_EQUAL_UINT8(EDB_CRYPTO_VERSION_CHACHA20, hdr.enc_version);
  TEST_ASSERT_EQUAL_UINT8(EDB_CRYPTO_MODE_BLIND, hdr.enc_mode);
  TEST_ASSERT_EQUAL_UINT16(8, hdr.plaintext_rec_size);
  TEST_ASSERT_EQUAL_UINT16(36, hdr.stored_rec_size);
  for (size_t i = EDB_CRYPTO_WRAPPED_KEY_OFFSET; i < EDB_CRYPTO_EXT_SIZE; i++)
    TEST_ASSERT_EQUAL_UINT8(0, buf[i]);
}

void test_crypto_ext_blind_same_size_as_autonomous() {
  uint8_t blind[EDB_CRYPTO_EXT_SIZE];
  uint8_t salt[16] = {0};
  TEST_ASSERT_EQUAL_UINT32(EDB_CRYPTO_EXT_SIZE,
      (unsigned)edb_crypto_write_ext_blind(blind, salt, 8, 24));
#if defined(EDB_CRYPTO_DEVICE_AUTONOMOUS)
  uint8_t auto_ext[EDB_CRYPTO_EXT_SIZE];
  uint8_t wkey[48] = {0};
  TEST_ASSERT_EQUAL_UINT32(EDB_CRYPTO_EXT_SIZE,
      (unsigned)edb_crypto_write_ext_autonomous(auto_ext, salt, 8, 24, wkey));
#endif
}

void test_crypto_ext_invalid_magic() {
  uint8_t buf[24] = {0};
  EDB_CryptoExtHeader hdr;
  TEST_ASSERT_EQUAL_UINT32(0, (unsigned)edb_crypto_parse_ext(buf, 24, &hdr));
}

int run_crypto_header_tests() {
  UNITY_BEGIN();
  RUN_TEST(test_crypto_ext_blind_parse);
  RUN_TEST(test_crypto_ext_blind_same_size_as_autonomous);
  RUN_TEST(test_crypto_ext_invalid_magic);
  return UNITY_END();
}
#else
int run_crypto_header_tests() { return 0; }
#endif
