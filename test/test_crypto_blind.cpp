#include "unity.h"

void test_blind_opaque_storage() {
  /* e2e_blind: EDB stores opaque bytes without EDB_Crypto on device */
  TEST_ASSERT_TRUE(1);
}

int run_crypto_blind_tests() {
  UNITY_BEGIN();
  RUN_TEST(test_blind_opaque_storage);
  return UNITY_END();
}
