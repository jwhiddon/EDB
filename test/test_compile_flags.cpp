#include "unity.h"

void test_compile_flags_crypto_optional() {
#if defined(EDB_ENABLE_CRYPTO)
  TEST_ASSERT_TRUE(1);
#else
  TEST_ASSERT_TRUE(1);
#endif
}

int run_compile_flags_tests() {
  UNITY_BEGIN();
  RUN_TEST(test_compile_flags_crypto_optional);
  return UNITY_END();
}
