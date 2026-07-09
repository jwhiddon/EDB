#include "TestHelpers.h"
#include "FakeStorage.h"
#include "unity.h"
#include <cstdio>

int run_smoke_tests();
int run_integrity_tests();
int run_compat_tests();

int main() {
    setvbuf(stdout, NULL, _IONBF, 0);
    FakeStorage::usePrimary();
    int failures = 0;
    printf("SUITE smoke\n");
    failures += run_smoke_tests();
    printf("SUITE integrity (failures so far: %d)\n", failures);
    failures += run_integrity_tests();
    printf("SUITE compat (failures so far: %d)\n", failures);
    failures += run_compat_tests();
    printf("DONE failures=%d\n", failures);
    return failures == 0 ? 0 : 1;
}
