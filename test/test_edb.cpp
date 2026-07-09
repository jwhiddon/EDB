#include <cstring>
#include <vector>

#include "TestHelpers.h"
#include "test_dbs.h"
#include "unity.h"

#define TABLE_SIZE 256
#define REC_SIZE 4

static void seedDatabase(unsigned long n_recs) {
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    for (unsigned long i = 1; i <= n_recs; i++) {
        TestRecord record = makeRecord((int32_t)i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
}

/* ---- version-agnostic behavior (runs on both 1.0.7 and 3.x) ---- */

void test_create_basic() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    TEST_ASSERT_EQUAL_INT(EDB_FLAG, FakeStorage::instance().data[0]);
    TEST_ASSERT_EQUAL_UINT32(0, byteDb.count());
    TEST_ASSERT_TRUE(byteDb.limit() >= 8);
}

void test_create_rejects_invalid_params() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.create(0, TABLE_SIZE, 0));
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.create(0, 8, REC_SIZE));
}

void test_open_valid_database() {
    resetStorage();
    seedDatabase(3);
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    TEST_ASSERT_EQUAL_UINT32(3, reopened.count());
}

void test_open_rejects_corrupt_flag() {
    resetStorage();
    FakeStorage::instance().resize(TABLE_SIZE);
    FakeStorage::instance().data[0] = 0x00;
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.open(0));
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
    seedDatabase(2);
    TestRecord record = makeRecord(99);
    TEST_ASSERT_EQUAL_INT(EDB_OUT_OF_RANGE, byteDb.updateRec(0, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(EDB_OUT_OF_RANGE, byteDb.updateRec(3, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.updateRec(2, EDB_REC record));
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(2, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(99, readBack.value);
}

void test_table_full_errors() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    unsigned long limit = byteDb.limit();
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
    seedDatabase(2);
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

#ifndef EDB_VERSION
/* ---- legacy 1.0.7 shift semantics (recno renumbers on delete/insert) ---- */

void test_delete_rec_shifts_records() {
    resetStorage();
    seedDatabase(3);
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
    seedDatabase(3);
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

void test_malloc_failure_returns_error() {
    resetStorage();
    seedDatabase(3);
    EDB::setMallocFail(true);
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.deleteRec(1));
    TEST_ASSERT_EQUAL_UINT32(3, byteDb.count());
    EDB::setMallocFail(false);
}
#endif

#if EDB_VERSION
/* ---- v3 stable-slot semantics ---- */

void test_create_writes_v3_header() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    TEST_ASSERT_EQUAL_INT(EDB_FLAG, FakeStorage::instance().data[0]);
    TEST_ASSERT_EQUAL_INT(EDB_VERSION, FakeStorage::instance().data[1]);
    unsigned int stride = 1 + REC_SIZE + 2;
    TEST_ASSERT_EQUAL_UINT32((TABLE_SIZE - EDB_HEADER_SPAN) / stride, byteDb.limit());
}

void test_delete_tombstones_stable_ids() {
    resetStorage();
    seedDatabase(3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(2));
    TEST_ASSERT_EQUAL_UINT32(2, byteDb.count());
    TestRecord record;
    TEST_ASSERT_EQUAL_INT(EDB_DELETED, byteDb.readRec(2, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(1, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(1, record.value);          // id 1 unchanged
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(3, EDB_REC record));
    TEST_ASSERT_EQUAL_INT(3, record.value);          // id 3 not renumbered
}

void test_delete_freelist_reuse() {
    resetStorage();
    seedDatabase(3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(2));
    TestRecord record = makeRecord(222);
    unsigned long id = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record, &id));
    TEST_ASSERT_EQUAL_UINT32(2, id);                 // freed slot 2 reused
    TEST_ASSERT_EQUAL_UINT32(3, byteDb.count());
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(2, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(222, readBack.value);
}

void test_iteration_skips_tombstones() {
    resetStorage();
    seedDatabase(4);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(2));
    assertLiveSequence(byteDb, {1, 3, 4});
}

void test_insert_allocates_free_slot() {
    resetStorage();
    seedDatabase(3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(1));
    TestRecord record = makeRecord(111);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.insertRec(99 /* ignored */, EDB_REC record));
    TEST_ASSERT_EQUAL_UINT32(3, byteDb.count());
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(1, EDB_REC readBack));  // reused slot 1
    TEST_ASSERT_EQUAL_INT(111, readBack.value);
}

void test_legacy_v1_needs_migration() {
    resetStorage();
    seedV1AvrDatabase(2, REC_SIZE, 128);
    TEST_ASSERT_EQUAL_INT(EDB_NEEDS_MIGRATION, byteDb.open(0));
}

void test_corrupt_record_detected() {
    resetStorage();
    seedDatabase(2);
    // Flip a payload byte of slot 0 (record 1): status(1) at data_offset, payload follows.
    FakeStorage::instance().data[EDB_HEADER_SPAN + 1] ^= 0xFF;
    TestRecord record;
    TEST_ASSERT_EQUAL_INT(EDB_CORRUPT, byteDb.readRec(1, EDB_REC record));
}

void test_compact_reconciles() {
    resetStorage();
    seedDatabase(6);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(3));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(5));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.compact());
    TEST_ASSERT_EQUAL_UINT32(4, byteDb.count());
    assertLiveSequence(byteDb, {1, 2, 4, 6});
}

void test_buffer_delete_does_io() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.create(0, TABLE_SIZE, REC_SIZE));
    for (int i = 1; i <= 4; i++) {
        TestRecord record = makeRecord(i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.appendRec(EDB_REC record));
    }
    FakeStorage::instance().buffer_writes = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, bufferDb.deleteRec(2));
    TEST_ASSERT_TRUE(FakeStorage::instance().buffer_writes >= 1);
}

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
    const unsigned long TABLE_A = 256;
    const unsigned long TABLE_B = 256;
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
    RUN_TEST(test_create_basic);
    RUN_TEST(test_create_rejects_invalid_params);
    RUN_TEST(test_open_valid_database);
    RUN_TEST(test_open_rejects_corrupt_flag);
    RUN_TEST(test_append_and_read_round_trip);
    RUN_TEST(test_update_rec_bounds);
    RUN_TEST(test_table_full_errors);
    RUN_TEST(test_clear_resets_count);
    RUN_TEST(test_buffer_handlers_smoke);
#ifndef EDB_VERSION
    RUN_TEST(test_delete_rec_shifts_records);
    RUN_TEST(test_insert_rec_at_start_middle_end);
    RUN_TEST(test_insert_empty_table_requires_recno_one);
    RUN_TEST(test_malloc_failure_returns_error);
#endif
#if EDB_VERSION
    RUN_TEST(test_create_writes_v3_header);
    RUN_TEST(test_delete_tombstones_stable_ids);
    RUN_TEST(test_delete_freelist_reuse);
    RUN_TEST(test_iteration_skips_tombstones);
    RUN_TEST(test_insert_allocates_free_slot);
    RUN_TEST(test_legacy_v1_needs_migration);
    RUN_TEST(test_corrupt_record_detected);
    RUN_TEST(test_compact_reconciles);
    RUN_TEST(test_buffer_delete_does_io);
    RUN_TEST(test_next_table_offset);
    RUN_TEST(test_open_or_create);
    RUN_TEST(test_multi_table_isolated_counts);
#endif
    return UNITY_END();
}
