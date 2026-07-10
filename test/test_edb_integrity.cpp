#include "TestHelpers.h"
#include "test_dbs.h"
#include "unity.h"

#include <cstring>

#define TABLE_SIZE 256
#define REC_SIZE 4

/* ---- version-agnostic ---- */

void test_reopen_after_append() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 6);
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    assertAllRecords(reopened, {1, 2, 3, 4, 5, 6});
}

void test_append_full_leaves_data_intact() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    unsigned long limit = byteDb.limit();
    appendSequence(byteDb, 0, (int)limit - 1);
    auto before = FakeStorage::instance().snapshot();
    TestRecord extra = makeRecord(999);
    TEST_ASSERT_EQUAL_INT(EDB_TABLE_FULL, byteDb.appendRec(EDB_REC extra));
    auto after = FakeStorage::instance().snapshot();
    assertRecordRegionEquals(before, after);
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

void test_byte_buffer_append_parity() {
    runParityTest([](EDB& db) {
        TestRecord appended = makeRecord(77);
        TEST_ASSERT_EQUAL_INT(EDB_OK, db.appendRec(EDB_REC appended));
    });
}

#ifndef EDB_VERSION
/* ---- legacy 1.0.7 shift + malloc-shift behavior ---- */

static void runShiftDeleteTests(EDB& db) {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(db, 1, 8);
    TEST_ASSERT_EQUAL_INT(EDB_OK, db.deleteRec(1));
    assertAllRecords(db, {2, 3, 4, 5, 6, 7, 8});

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
}

void test_shift_delete_byte() { runShiftDeleteTests(byteDb); }
void test_shift_delete_buffer() { runShiftDeleteTests(bufferDb); }
void test_shift_insert_byte() { runShiftInsertTests(byteDb); }
void test_shift_insert_buffer() { runShiftInsertTests(bufferDb); }

void test_malloc_fail_delete_preserves_data() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 5);
    EDB::setMallocFail(true);
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.deleteRec(2));
    EDB::setMallocFail(false);
    TEST_ASSERT_EQUAL_UINT32(5, byteDb.count());
    assertAllRecords(byteDb, {1, 2, 3, 4, 5});
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
    assertAllRecords(reopened, {1, 99, 50, 4, 5});
}
#endif

#if EDB_VERSION
/* ---- v3 stable-slot integrity + crash-safety ---- */

void test_v3_delete_preserves_other_slots() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 5);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(3));
    assertLiveSequence(byteDb, {1, 2, 4, 5});
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    assertLiveSequence(reopened, {1, 2, 4, 5});
}

void test_v3_reopen_after_mixed_ops() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 5);
    TestRecord updated = makeRecord(50);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.updateRec(3, EDB_REC updated)); // slot 2 -> 50
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(2));                  // tombstone slot 1
    TestRecord inserted = makeRecord(99);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.insertRec(0, EDB_REC inserted)); // reuses slot 1
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    assertLiveSequence(reopened, {1, 99, 50, 4, 5});
}

void test_header_nlive_matches_count() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 3);
    std::vector<uint8_t>& d = FakeStorage::instance().data;
    uint32_t seq0, seq1, live0, live1;
    memcpy(&seq0, &d[4], 4);
    memcpy(&seq1, &d[EDB_HEADER_COPY_SIZE + 4], 4);
    memcpy(&live0, &d[12], 4);
    memcpy(&live1, &d[EDB_HEADER_COPY_SIZE + 12], 4);
    uint32_t active_live = (seq0 >= seq1) ? live0 : live1;   // newest copy is authoritative
    TEST_ASSERT_EQUAL_UINT32(3, active_live);
    TEST_ASSERT_EQUAL_UINT32(byteDb.count(), active_live);
}

void test_torn_older_header_recovers() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 4);
    std::vector<uint8_t>& d = FakeStorage::instance().data;
    uint32_t seq0, seq1;
    memcpy(&seq0, &d[4], 4);
    memcpy(&seq1, &d[EDB_HEADER_COPY_SIZE + 4], 4);
    unsigned older = (seq0 <= seq1) ? 0u : 1u;
    d[older * EDB_HEADER_COPY_SIZE + 5] ^= 0xFF;   // corrupt a crc-covered byte of the older copy
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    TEST_ASSERT_EQUAL_UINT32(4, reopened.count());
}

// Both header copies corrupt (not just the stale one) -> open() reports EDB_ERROR, never a
// silent bad read. Complements test_torn_older_header_recovers (which corrupts only the older copy).
void test_v3_corrupt_both_headers_error() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 4);
    std::vector<uint8_t>& d = FakeStorage::instance().data;
    d[5] ^= 0xFF;                              // corrupt a CRC-covered byte (seq) of copy 0
    d[EDB_HEADER_COPY_SIZE + 5] ^= 0xFF;       // and of copy 1 (magic/version left intact)
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, reopened.open(0));
}

// O(1) delete must tombstone only the target slot and never rewrite/shift the others: the bytes of
// every untouched slot are identical before and after the delete.
void test_v3_delete_leaves_other_slots_byte_identical() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 6);
    std::vector<uint8_t> before = FakeStorage::instance().snapshot();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(3));   // tombstone slot index 2
    std::vector<uint8_t> after = FakeStorage::instance().snapshot();

    const unsigned long base = EDB_HEADER_SPAN;           // default data_offset
    const unsigned long stride = 1 + REC_SIZE + 2;
    for (int idx = 0; idx < 6; idx++) {
        if (idx == 2) continue;                           // the deleted slot is expected to change
        unsigned long off = base + (unsigned long)idx * stride;
        for (unsigned long b = 0; b < stride; b++)
            TEST_ASSERT_EQUAL_UINT8(before[off + b], after[off + b]);
    }
}

// Crash-safety fault injection: tear an append after every possible number of byte writes and
// assert a fresh open() always yields a consistent table (the old N-record state or the fully
// applied N+1 state), never a corrupt read. Covers both the torn-slot and torn-header windows,
// since an append writes the slot (payload, crc, status-last) and then publishes the header.
void test_v3_torn_write_is_always_consistent() {
    const int N = 6;
    const int32_t TORN_VALUE = 0x01020304;   // all bytes non-0x00/0xFF -> deterministic write count

    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, N);
    std::vector<uint8_t> good = FakeStorage::instance().snapshot();   // last consistent state

    // Cost (in byte writes) of one full append, structurally identical to the torn ones.
    FakeStorage::instance().resetCounters();
    TestRecord probe = makeRecord(TORN_VALUE);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC probe));   // commits N+1 (discarded below)
    long full = (long)FakeStorage::instance().byte_writes;
    TEST_ASSERT_TRUE(full > 0);

    for (long k = 1; k <= full; k++) {
        FakeStorage::instance().load(good);                          // restore state, clear fault
        EDB torn(&FakeStorage::writeByte, &FakeStorage::readByte);
        TEST_ASSERT_EQUAL_INT(EDB_OK, torn.open(0));                 // open() only reads
        FakeStorage::instance().failAfter(k);                        // power loss after k writes
        TestRecord extra = makeRecord(TORN_VALUE);
        torn.appendRec(EDB_REC extra);                              // torn at offset k; ignore result
        FakeStorage::instance().clearFault();

        EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
        TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));            // a valid header always survives
        unsigned long c = reopened.count();
        TEST_ASSERT_TRUE(c == (unsigned long)N || c == (unsigned long)(N + 1));
        int seen = 0;
        for (unsigned long r = reopened.firstRec(); r != 0; r = reopened.nextRec(r)) {
            TestRecord rec;
            TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.readRec(r, EDB_REC rec));   // every live rec CRC-valid
            if (seen < N) TEST_ASSERT_EQUAL_INT(seen + 1, rec.value);          // originals 1..N intact
            seen++;
        }
        TEST_ASSERT_EQUAL_INT((int)c, seen);
    }
}

void test_v1_avr_needs_migration() {
    resetStorage();
    seedV1AvrDatabase(2, REC_SIZE, 128);
    TEST_ASSERT_EQUAL_INT(EDB_NEEDS_MIGRATION, byteDb.open(0));
}

void test_v1_esp32_needs_migration() {
    resetStorage();
    seedV1Esp32Database(2, REC_SIZE, 128);
    TEST_ASSERT_EQUAL_INT(EDB_NEEDS_MIGRATION, byteDb.open(0));
}
#endif

int run_integrity_tests() {
    UNITY_BEGIN();
    RUN_TEST(test_reopen_after_append);
    RUN_TEST(test_append_full_leaves_data_intact);
    RUN_TEST(test_nonzero_head_ptr);
    RUN_TEST(test_larger_record_struct);
    RUN_TEST(test_clear_wipes_logical_data);
    RUN_TEST(test_byte_buffer_delete_parity);
    RUN_TEST(test_byte_buffer_append_parity);
#ifndef EDB_VERSION
    RUN_TEST(test_shift_delete_byte);
    RUN_TEST(test_shift_delete_buffer);
    RUN_TEST(test_shift_insert_byte);
    RUN_TEST(test_shift_insert_buffer);
    RUN_TEST(test_malloc_fail_delete_preserves_data);
    RUN_TEST(test_reopen_after_mixed_ops);
#endif
#if EDB_VERSION
    RUN_TEST(test_v3_delete_preserves_other_slots);
    RUN_TEST(test_v3_reopen_after_mixed_ops);
    RUN_TEST(test_header_nlive_matches_count);
    RUN_TEST(test_torn_older_header_recovers);
    RUN_TEST(test_v3_corrupt_both_headers_error);
    RUN_TEST(test_v3_delete_leaves_other_slots_byte_identical);
    RUN_TEST(test_v3_torn_write_is_always_consistent);
    RUN_TEST(test_v1_avr_needs_migration);
    RUN_TEST(test_v1_esp32_needs_migration);
#endif
    return UNITY_END();
}
