#include "TestHelpers.h"
#include "FakeStorage.h"
#include "unity.h"
#include <cstdio>

int run_smoke_tests();
int run_integrity_tests();
int run_edge_tests();
int run_compat_tests();
int run_crypto_tests();
int run_crypto_header_tests();
int run_crypto_integrity_tests();
int run_crypto_blind_tests();
int run_compile_flags_tests();

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    FakeStorage::usePrimary();
    int failures = 0;
    printf("SUITE smoke\n");
    failures += run_smoke_tests();
    printf("SUITE integrity (failures so far: %d)\n", failures);
    failures += run_integrity_tests();
    printf("SUITE edge (failures so far: %d)\n", failures);
    failures += run_edge_tests();
    printf("SUITE compat (failures so far: %d)\n", failures);
    failures += run_compat_tests();
    printf("SUITE crypto (failures so far: %d)\n", failures);
    failures += run_crypto_tests();
    failures += run_crypto_header_tests();
    failures += run_crypto_integrity_tests();
    failures += run_crypto_blind_tests();
    failures += run_compile_flags_tests();
    printf("DONE failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
