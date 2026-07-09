#include "TestHelpers.h"
#include "FakeStorage.h"
#include "unity.h"

int run_smoke_tests();
int run_integrity_tests();
int run_compat_tests();

int main() {
    FakeStorage::usePrimary();
    int failures = 0;
    failures += run_smoke_tests();
    failures += run_integrity_tests();
    failures += run_compat_tests();
    return failures == 0 ? 0 : 1;
}
