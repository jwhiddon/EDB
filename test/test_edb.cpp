#include <cstring>
#include <vector>

#include "Arduino.h"
#include "EDB.h"
#include "FakeStorage.h"
#include "test_dbs.h"
#include "unity.h"

#define TABLE_SIZE 128
#define REC_SIZE 4

struct TestRecord {
    int32_t value;
};

static void resetStorage() {
    FakeStorage::instance().reset();
}

static TestRecord makeRecord(int32_t value) {
    TestRecord record;
    record.value = value;
    return record;
}

static void seedV2Database(unsigned long n_recs) {
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    for (unsigned long i = 1; i <= n_recs; i++) {
        TestRecord record = makeRecord((int32_t)i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
}

static void seedV1AvrDatabase(unsigned long n_recs) {
    FakeStorage::instance().resize(TABLE_SIZE);
    std::vector<uint8_t>& data = FakeStorage::instance().data;
    data.assign(TABLE_SIZE, 0xFF);
    data[0] = EDB_FLAG;
    data[1] = 0x00;
    memcpy(&data[4], &n_recs, sizeof(n_recs));
    uint16_t rec_size = REC_SIZE;
    memcpy(&data[8], &rec_size, sizeof(rec_size));
    uint16_t table_size = TABLE_SIZE;
    memcpy(&data[10], &table_size, sizeof(table_size));
    for (unsigned long i = 0; i < n_recs; i++) {
        int32_t value = (int32_t)(i + 1);
        memcpy(&data[12 + i * REC_SIZE], &value, sizeof(value));
    }
}

void test_create_writes_v2_header() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    TEST_ASSERT_EQUAL_INT(EDB_FLAG, FakeStorage::instance().data[0]);
#if EDB_VERSION
    TEST_ASSERT_EQUAL_INT(EDB_VERSION, FakeStorage::instance().data[1]);
    TEST_ASSERT_EQUAL_UINT32((TABLE_SIZE - 12) / REC_SIZE, byteDb.limit());
#else
    TEST_ASSERT_EQUAL_UINT32((TABLE_SIZE - sizeof(EDB_Header)) / REC_SIZE, byteDb.limit());
#endif
    TEST_ASSERT_EQUAL_UINT32(0, byteDb.count());
}

void test_create_rejects_invalid_params() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.create(0, TABLE_SIZE, 0));
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.create(0, 8, REC_SIZE));
}

void test_open_valid_v2_database() {
    resetStorage();
    seedV2Database(3);
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    TEST_ASSERT_EQUAL_UINT32(3, reopened.count());
}

void test_open_rejects_corrupt_flag() {
    resetStorage();
    FakeStorage::instance().resize(32);
    FakeStorage::instance().data[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.open(0));
}

void test_open_v1_avr_database() {
    resetStorage();
    seedV1AvrDatabase(2);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.open(0));
    TEST_ASSERT_EQUAL_UINT32(2, byteDb.count());
    TestRecord record;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(2, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(2, record.value);
}

void test_append_and_read_round_trip() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    for (int i = 1; i <= 5; i++) {
        TestRecord record = makeRecord(i * 10);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    for (int i = 1; i <= 5; i++) {
        TestRecord record;
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(i, EDB_REC record));
        TEST_ASSERT_EQUAL_INT(i * 10, record.value);
    }
}

void test_update_rec_bounds() {
    resetStorage();
    seedV2Database(2);
    TestRecord record = makeRecord(99);
    TEST_ASSERT_EQUAL_INT(EDB_OUT_OF_RANGE, byteDb.updateRec(0, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(EDB_OUT_OF_RANGE, byteDb.updateRec(3, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.updateRec(2, EDB_REC record));
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(2, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(99, readBack.value);
}

void test_delete_rec_shifts_records() {
    resetStorage();
    seedV2Database(3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(2));
    TEST_ASSERT_EQUAL_UINT32(2, byteDb.count());
    TestRecord record;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(1, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(1, record.value);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(2, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(3, record.value);
}

void test_insert_rec_at_start_middle_end() {
    resetStorage();
    seedV2Database(3);
    TestRecord record = makeRecord(100);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.insertRec(2, EDB_REC record));
    TEST_ASSERT_EQUAL_UINT32(4, byteDb.count());
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(2, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(100, readBack.value);
}

void test_insert_empty_table_requires_recno_one() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    TestRecord record = makeRecord(1);
    TEST_ASSERT_EQUAL_INT(EDB_OUT_OF_RANGE, byteDb.insertRec(0, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.insertRec(1, EDB_REC record));
    TEST_ASSERT_EQUAL_UINT32(1, byteDb.count());
}

void test_table_full_errors() {
    resetStorage();
    unsigned long limit = (TABLE_SIZE - 12) / REC_SIZE;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    for (unsigned long i = 0; i < limit; i++) {
        TestRecord record = makeRecord((int32_t)i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    TestRecord extra = makeRecord(999);
    TEST_ASSERT_EQUAL_INT(EDB_TABLE_FULL, byteDb.appendRec(EDB_REC extra));
    TEST_ASSERT_EQUAL_INT(EDB_TABLE_FULL, byteDb.insertRec(1, EDB_REC extra));
}

void test_clear_resets_count() {
    resetStorage();
    seedV2Database(2);
#if EDB_VERSION
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.clear());
#else
    byteDb.clear();
#endif
    TEST_ASSERT_EQUAL_UINT32(0, byteDb.count());
}

void test_buffer_handlers_smoke() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.create(0, TABLE_SIZE, REC_SIZE));
    TestRecord record = makeRecord(42);
    TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.appendRec(EDB_REC record));
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.readRec(1, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(42, readBack.value);
}

void test_buffer_delete_uses_block_shift() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.create(0, TABLE_SIZE, REC_SIZE));
    for (int i = 1; i <= 4; i++) {
        TestRecord record = makeRecord(i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.appendRec(EDB_REC record));
    }
    FakeStorage::instance().buffer_reads = 0;
    FakeStorage::instance().buffer_writes = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.deleteRec(2));
    TEST_ASSERT_EQUAL_INT(1, FakeStorage::instance().buffer_reads);
    TEST_ASSERT_EQUAL_INT(1, FakeStorage::instance().buffer_writes);
}

void test_malloc_failure_returns_error() {
    resetStorage();
    seedV2Database(3);
    EDB::setMallocFail(true);
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.deleteRec(1));
    TEST_ASSERT_EQUAL_UINT32(3, byteDb.count());
    EDB::setMallocFail(false);
}

#if EDB_VERSION
void test_next_table_offset() {
    TEST_ASSERT_EQUAL_UINT32(512, EDB::nextTableOffset(0, 512));
    TEST_ASSERT_EQUAL_UINT32(640, EDB::nextTableOffset(512, 128));
}

void test_open_or_create() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.openOrCreate(0, TABLE_SIZE, REC_SIZE));
    TEST_ASSERT_EQUAL_UINT32(0, byteDb.count());
    TestRecord record = makeRecord(7);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.openOrCreate(0, TABLE_SIZE, REC_SIZE));
    TEST_ASSERT_EQUAL_UINT32(1, byteDb.count());
}

void test_multi_table_isolated_counts() {
    resetStorage();
    const unsigned long TABLE_A = 128;
    const unsigned long TABLE_B = 128;
    const unsigned long HEAD_B = EDB::nextTableOffset(0, TABLE_A);

    EDB dbA(&FakeStorage::writeByte, &FakeStorage::readByte);
    EDB dbB(&FakeStorage::writeByte, &FakeStorage::readByte);

    TEST_ASSERT_EQUAL_INT(EDB_OK, dbA.openOrCreate(0, TABLE_A, REC_SIZE));
    TEST_ASSERT_EQUAL_INT(EDB_OK, dbB.openOrCreate(HEAD_B, TABLE_B, REC_SIZE));

    TestRecord a = makeRecord(11);
    TestRecord b = makeRecord(22);
    TEST_ASSERT_EQUAL_INT(EDB_OK, dbA.appendRec(EDB_REC a));
    TEST_ASSERT_EQUAL_INT(EDB_OK, dbB.appendRec(EDB_REC b));
    TEST_ASSERT_EQUAL_INT(EDB_OK, dbB.appendRec(EDB_REC b));

    TEST_ASSERT_EQUAL_UINT32(1, dbA.count());
    TEST_ASSERT_EQUAL_UINT32(2, dbB.count());
    TEST_ASSERT_EQUAL_UINT32(0, dbA.headPtr());
    TEST_ASSERT_EQUAL_UINT32(HEAD_B, dbB.headPtr());

    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, dbA.readRec(1, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(11, readBack.value);
    TEST_ASSERT_EQUAL_INT(EDB_OK, dbB.readRec(2, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(22, readBack.value);
}
#endif

int run_smoke_tests() {
    UNITY_BEGIN();
    RUN_TEST(test_create_writes_v2_header);
    RUN_TEST(test_create_rejects_invalid_params);
    RUN_TEST(test_open_valid_v2_database);
    RUN_TEST(test_open_rejects_corrupt_flag);
#if EDB_VERSION
    RUN_TEST(test_open_v1_avr_database);
#endif
    RUN_TEST(test_append_and_read_round_trip);
    RUN_TEST(test_update_rec_bounds);
    RUN_TEST(test_delete_rec_shifts_records);
    RUN_TEST(test_insert_rec_at_start_middle_end);
    RUN_TEST(test_insert_empty_table_requires_recno_one);
    RUN_TEST(test_table_full_errors);
    RUN_TEST(test_clear_resets_count);
    RUN_TEST(test_buffer_handlers_smoke);
    RUN_TEST(test_buffer_delete_uses_block_shift);
    RUN_TEST(test_malloc_failure_returns_error);
#if EDB_VERSION
    RUN_TEST(test_next_table_offset);
    RUN_TEST(test_open_or_create);
    RUN_TEST(test_multi_table_isolated_counts);
#endif
    return UNITY_END();
}
