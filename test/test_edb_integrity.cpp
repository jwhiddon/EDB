#include "TestHelpers.h"
#include "test_dbs.h"
#include "unity.h"

#define TABLE_SIZE 128
#define REC_SIZE 4

static void runShiftDeleteTests(EDB& db) {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(db, 1, 8);

    TEST_ASSERT_EQUAL_INT(EDB_OK, db.deleteRec(1));
    assertAllRecords(db, {2, 3, 4, 5, 6, 7, 8});

    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(db, 1, 8);
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.deleteRec(8));
    assertAllRecords(db, {1, 2, 3, 4, 5, 6, 7});

    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(db, 1, 8);
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.deleteRec(4));
    assertAllRecords(db, {1, 2, 3, 5, 6, 7, 8});
}

static void runShiftInsertTests(EDB& db) {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(db, 1, 5);
    TestRecord inserted = makeRecord(99);
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.insertRec(1, EDB_REC inserted));
    assertAllRecords(db, {99, 1, 2, 3, 4, 5});

    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(db, 1, 5);
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.insertRec(5, EDB_REC inserted));
    assertAllRecords(db, {1, 2, 3, 4, 99, 5});

    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(db, 1, 5);
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.insertRec(3, EDB_REC inserted));
    assertAllRecords(db, {1, 2, 99, 3, 4, 5});
}

void test_shift_delete_byte() { runShiftDeleteTests(byteDb); }
void test_shift_delete_buffer() { runShiftDeleteTests(bufferDb); }
void test_shift_insert_byte() { runShiftInsertTests(byteDb); }
void test_shift_insert_buffer() { runShiftInsertTests(bufferDb); }

void test_append_full_leaves_data_intact() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    unsigned long limit = byteDb.limit();
    appendSequence(byteDb, 0, (int)limit - 1);
    auto before = snapshotRecordRegion(0, v2HeaderSize(), limit, REC_SIZE);
    TestRecord extra = makeRecord(999);
    TEST_ASSERT_EQUAL_INT(EDB_TABLE_FULL, byteDb.appendRec(EDB_REC extra));
    auto after = snapshotRecordRegion(0, v2HeaderSize(), limit, REC_SIZE);
    assertRecordRegionEquals(before, after);
}

void test_insert_full_leaves_data_intact() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    unsigned long limit = byteDb.limit();
    appendSequence(byteDb, 0, (int)limit - 1);
    auto before = snapshotRecordRegion(0, v2HeaderSize(), limit, REC_SIZE);
    TestRecord extra = makeRecord(999);
    TEST_ASSERT_EQUAL_INT(EDB_TABLE_FULL, byteDb.insertRec(1, EDB_REC extra));
    auto after = snapshotRecordRegion(0, v2HeaderSize(), limit, REC_SIZE);
    assertRecordRegionEquals(before, after);
}

void test_malloc_fail_delete_preserves_data() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 5);
    auto before = snapshotRecordRegion(0, v2HeaderSize(), 5, REC_SIZE);
    EDB::setMallocFail(true);
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.deleteRec(2));
    EDB::setMallocFail(false);
    TEST_ASSERT_EQUAL_UINT32(5, byteDb.count());
    assertAllRecords(byteDb, {1, 2, 3, 4, 5});
    assertRecordRegionEquals(before, snapshotRecordRegion(0, v2HeaderSize(), 5, REC_SIZE));
}

void test_malloc_fail_insert_preserves_data() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 5);
    auto before = snapshotRecordRegion(0, v2HeaderSize(), 5, REC_SIZE);
    TestRecord inserted = makeRecord(99);
    EDB::setMallocFail(true);
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.insertRec(2, EDB_REC inserted));
    EDB::setMallocFail(false);
    TEST_ASSERT_EQUAL_UINT32(5, byteDb.count());
    assertAllRecords(byteDb, {1, 2, 3, 4, 5});
    assertRecordRegionEquals(before, snapshotRecordRegion(0, v2HeaderSize(), 5, REC_SIZE));
}

void test_reopen_after_append() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 6);
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    assertAllRecords(reopened, {1, 2, 3, 4, 5, 6});
}

void test_reopen_after_mixed_ops() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 5);
    TestRecord updated = makeRecord(50);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.updateRec(3, EDB_REC updated));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(2));
    TestRecord inserted = makeRecord(99);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.insertRec(2, EDB_REC inserted));
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    assertAllRecords(reopened, {1, 99, 3, 50, 4, 5});
}

void test_header_nrecs_matches_count() {
#if EDB_VERSION
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 3);
    unsigned long n_recs = 0;
    memcpy(&n_recs, &FakeStorage::instance().data[2], sizeof(n_recs));
    TEST_ASSERT_EQUAL_UINT32(3, n_recs);
    TEST_ASSERT_EQUAL_UINT32(byteDb.count(), n_recs);
#endif
}

template<typename Mutator>
static void runParityTest(Mutator mutate) {
    FakeStorage::usePrimary();
    resetStorage();
    EDB byteSide(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteSide.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteSide, 1, 4);
    mutate(byteSide);
    auto byteSnap = FakeStorage::instance().snapshot();

    FakeStorage::useSecondary();
    resetStorage();
    EDB bufferSide(&FakeStorage::writeBuffer, &FakeStorage::readBuffer);
    TEST_ASSERT_EQUAL_INT(EDB_OK, bufferSide.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(bufferSide, 1, 4);
    mutate(bufferSide);
    auto bufferSnap = FakeStorage::instance().snapshot();

    TEST_ASSERT_EQUAL_INT((int)byteSnap.size(), (int)bufferSnap.size());
    assertRecordRegionEquals(byteSnap, bufferSnap);
    FakeStorage::usePrimary();
}

void test_byte_buffer_delete_parity() {
    runParityTest([](EDB& db) { TEST_ASSERT_EQUAL_INT(EDB_OK, db.deleteRec(2)); });
}

void test_byte_buffer_insert_parity() {
    runParityTest([](EDB& db) {
        TestRecord inserted = makeRecord(77);
        TEST_ASSERT_EQUAL_INT(EDB_OK, db.insertRec(2, EDB_REC inserted));
    });
}

void test_nonzero_head_ptr() {
    resetStorage();
    const unsigned long head = 16;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(head, TABLE_SIZE, REC_SIZE));
    TestRecord record = makeRecord(42);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(1, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(42, readBack.value);
}

struct WideRecord {
    int32_t a;
    int32_t b;
};

void test_larger_record_struct() {
    resetStorage();
    EDB wideDb(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, wideDb.create(0, TABLE_SIZE, sizeof(WideRecord)));
    WideRecord record;
    record.a = 11;
    record.b = 22;
    TEST_ASSERT_EQUAL_INT(EDB_OK, wideDb.appendRec(EDB_REC record));
    TEST_ASSERT_EQUAL_INT(EDB_OK, wideDb.deleteRec(1));
    TEST_ASSERT_EQUAL_UINT32(0, wideDb.count());
}

void test_clear_wipes_logical_data() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 2);
#if EDB_VERSION
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.clear());
#else
    byteDb.clear();
#endif
    TEST_ASSERT_EQUAL_UINT32(0, byteDb.count());
    TestRecord record;
    TEST_ASSERT_EQUAL_INT(EDB_OUT_OF_RANGE, byteDb.readRec(1, EDB_REC record));
    TestRecord fresh = makeRecord(5);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC fresh));
    assertAllRecords(byteDb, {5});
}

#if EDB_VERSION
void test_v1_avr_update_upgrades_header() {
    resetStorage();
    seedV1AvrDatabase(2, REC_SIZE, TABLE_SIZE);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.open(0));
    TestRecord updated = makeRecord(77);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.updateRec(1, EDB_REC updated));
    TEST_ASSERT_EQUAL_INT(EDB_VERSION, FakeStorage::instance().data[1]);
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    assertAllRecords(reopened, {77, 2});
}

void test_v1_avr_delete_preserves_records() {
    resetStorage();
    seedV1AvrDatabase(3, REC_SIZE, TABLE_SIZE);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.open(0));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(2));
    assertAllRecords(byteDb, {1, 3});
}

void test_v1_esp32_write_rejected_without_migration() {
    resetStorage();
    seedV1Esp32Database(2, REC_SIZE, TABLE_SIZE);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.open(0));
    TestRecord updated = makeRecord(99);
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.updateRec(1, EDB_REC updated));
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.appendRec(EDB_REC updated));
    assertAllRecords(byteDb, {1, 2});
}
#endif

int run_integrity_tests() {
    UNITY_BEGIN();
    RUN_TEST(test_shift_delete_byte);
    RUN_TEST(test_shift_delete_buffer);
    RUN_TEST(test_shift_insert_byte);
    RUN_TEST(test_shift_insert_buffer);
    RUN_TEST(test_append_full_leaves_data_intact);
    RUN_TEST(test_insert_full_leaves_data_intact);
    RUN_TEST(test_malloc_fail_delete_preserves_data);
    RUN_TEST(test_malloc_fail_insert_preserves_data);
    RUN_TEST(test_reopen_after_append);
    RUN_TEST(test_reopen_after_mixed_ops);
    RUN_TEST(test_header_nrecs_matches_count);
    RUN_TEST(test_byte_buffer_delete_parity);
    RUN_TEST(test_byte_buffer_insert_parity);
    RUN_TEST(test_nonzero_head_ptr);
    RUN_TEST(test_larger_record_struct);
    RUN_TEST(test_clear_wipes_logical_data);
#if EDB_VERSION
    RUN_TEST(test_v1_avr_update_upgrades_header);
    RUN_TEST(test_v1_avr_delete_preserves_records);
    RUN_TEST(test_v1_esp32_write_rejected_without_migration);
#endif
    return UNITY_END();
}
