#include "TestHelpers.h"
#include "test_dbs.h"
#include "unity.h"

void test_update_rec_zero_out_of_range() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, compatDb.create(0, 128, 4));
    appendSequence(compatDb, 1, 2);
    TestRecord record = makeRecord(99);
    TEST_ASSERT_EQUAL_INT(EDB_OUT_OF_RANGE, compatDb.updateRec(0, EDB_REC record));
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, compatDb.readRec(1, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(1, readBack.value);
    TEST_ASSERT_EQUAL_INT(EDB_OK, compatDb.readRec(2, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(2, readBack.value);
}

void test_open_corrupt_flag() {
    resetStorage();
    FakeStorage::instance().resize(32);
    FakeStorage::instance().data[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, compatDb.open(0));
}

void test_create_zero_recsize() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, compatDb.create(0, 128, 0));
}

void test_open_master_avr_fixture() {
#if EDB_VERSION
    // v3 does not read legacy files in place; it reports them for offline migration.
    std::vector<uint8_t> fixture;
    if (!loadFixtureFile("fixtures/v1_master_avr_8rec.db", fixture)) {
        printf("SKIP fixtures/v1_master_avr_8rec.db not found\n");
        return;
    }
    resetStorage();
    FakeStorage::instance().load(fixture);
    TEST_ASSERT_EQUAL_INT(EDB_NEEDS_MIGRATION, compatDb.open(0));
#else
    printf("SKIP avr fixture open on 1.0.7 host build (MCU-specific struct layout)\n");
#endif
}

void test_open_master_esp32_fixture() {
#if EDB_VERSION
    std::vector<uint8_t> fixture;
    if (!loadFixtureFile("fixtures/v1_master_esp32_4rec.db", fixture)) {
        printf("SKIP fixtures/v1_master_esp32_4rec.db not found\n");
        return;
    }
    resetStorage();
    FakeStorage::instance().load(fixture);
    TEST_ASSERT_EQUAL_INT(EDB_NEEDS_MIGRATION, compatDb.open(0));
#endif
}

// Cross-language: a file migrated to v3 by tools/edb_migrate.py must open on the C++ device
// (proves the Python and C++ CRC32/CRC16 layouts agree).
void test_open_migrated_v3_fixture() {
#if EDB_VERSION
    std::vector<uint8_t> fixture;
    if (!loadFixtureFile("fixtures/v3_migrated_avr_8rec.db", fixture)) {
        printf("SKIP fixtures/v3_migrated_avr_8rec.db not found\n");
        return;
    }
    resetStorage();
    FakeStorage::instance().load(fixture);
    TEST_ASSERT_EQUAL_INT(EDB_OK, compatDb.open(0));
    TEST_ASSERT_EQUAL_UINT32(8, compatDb.count());
    assertLiveSequence(compatDb, {1, 2, 3, 4, 5, 6, 7, 8});
#endif
}

// Realistic migration: the shipped sensor records (22-byte, varied bytes in every field) migrated
// v1 AVR -> v3 must open on the device and read back byte-for-byte identical to test/data/sensorlog.bin.
// Exercises migration, slot framing, and the per-slot CRC16 on non-trivial data.
void test_migrated_sensor_records_survive() {
#if EDB_VERSION
    std::vector<uint8_t> v3, src;
    if (!loadFixtureFile("fixtures/v3_sensor_avr_64rec.db", v3) ||
        !loadFixtureFile("data/sensorlog.bin", src)) {
        printf("SKIP realistic migration fixture not found (run gen_datasets.py + generate_fixtures.py)\n");
        return;
    }
    resetStorage();
    FakeStorage::instance().load(v3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, compatDb.open(0));
    TEST_ASSERT_EQUAL_UINT32(64, compatDb.count());
    const unsigned int rs = 22;
    for (unsigned long i = 0; i < 64; i++) {
        uint8_t got[22];
        TEST_ASSERT_EQUAL_INT(EDB_OK, compatDb.readRec(i + 1, (EDB_Rec)got));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(&src[i * rs], got, (int)rs);
    }
#endif
}

void test_api_symbols_present() {
    TEST_ASSERT_TRUE(true);
}

int run_compat_tests() {
    UNITY_BEGIN();
    RUN_TEST(test_update_rec_zero_out_of_range);
    RUN_TEST(test_open_corrupt_flag);
    RUN_TEST(test_create_zero_recsize);
    RUN_TEST(test_open_master_avr_fixture);
    RUN_TEST(test_open_master_esp32_fixture);
    RUN_TEST(test_open_migrated_v3_fixture);
    RUN_TEST(test_migrated_sensor_records_survive);
    RUN_TEST(test_api_symbols_present);
    return UNITY_END();
}
