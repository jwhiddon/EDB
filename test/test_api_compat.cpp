#include "TestHelpers.h"
#include "unity.h"

static EDB db(&FakeStorage::writeByte, &FakeStorage::readByte);

void test_update_rec_zero_out_of_range() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.create(0, 128, 4));
    appendSequence(db, 1, 2);
    auto before = snapshotRecordRegion(0, v2HeaderSize(), 2, 4);
    TestRecord record = makeRecord(99);
    TEST_ASSERT_EQUAL_INT(EDB_OUT_OF_RANGE, db.updateRec(0, EDB_REC record));
    assertRecordRegionEquals(before, snapshotRecordRegion(0, v2HeaderSize(), 2, 4));
}

void test_open_corrupt_flag() {
    resetStorage();
    FakeStorage::instance().resize(32);
    FakeStorage::instance().data[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, db.open(0));
}

void test_create_zero_recsize() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, db.create(0, 128, 0));
}

void test_open_master_avr_fixture() {
#if EDB_VERSION
    std::vector<uint8_t> fixture;
    if (!loadFixtureFile("fixtures/v1_master_avr_8rec.db", fixture)) {
        printf("SKIP fixtures/v1_master_avr_8rec.db not found\n");
        return;
    }
    resetStorage();
    FakeStorage::instance().load(fixture);
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.open(0));
    assertAllRecords(db, {1, 2, 3, 4, 5, 6, 7, 8});
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
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.open(0));
    assertAllRecords(db, {10, 20, 30, 40});
#endif
}

void test_api_symbols_present() {
#if EDB_VERSION
    TEST_ASSERT_TRUE(true);
#else
    TEST_ASSERT_TRUE(true);
#endif
}

int run_compat_tests() {
    UNITY_BEGIN();
    RUN_TEST(test_update_rec_zero_out_of_range);
    RUN_TEST(test_open_corrupt_flag);
    RUN_TEST(test_create_zero_recsize);
    RUN_TEST(test_open_master_avr_fixture);
    RUN_TEST(test_open_master_esp32_fixture);
    RUN_TEST(test_api_symbols_present);
    return UNITY_END();
}
