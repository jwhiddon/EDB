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

struct IdRecord {
    uint32_t id;
    int32_t value;
};

static IdRecord makeIdRecord(int32_t value) {
    IdRecord rec = {0, value};
    return rec;
}

void test_stable_ids_assigned_on_append() {
    resetStorage();
    const unsigned int REC = 8;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.enableStableIds());
    IdRecord a = makeIdRecord(10);
    unsigned long r1 = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC a, &r1));
    uint32_t id1 = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.recordId(r1, &id1));
    TEST_ASSERT_EQUAL_UINT32(1, id1);
    TEST_ASSERT_EQUAL_INT(10, a.value);

    IdRecord b = makeIdRecord(20);
    unsigned long r2 = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC b, &r2));
    uint32_t id2 = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.recordId(r2, &id2));
    TEST_ASSERT_EQUAL_UINT32(2, id2);

    unsigned long found = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.findRecById(id2, &found));
    TEST_ASSERT_EQUAL_UINT32(r2, found);
}

static unsigned long ringTableSize(unsigned int rec_size, unsigned long max_records) {
    return EDB_HEADER_SPAN + max_records * (1 + rec_size + 2);
}

void test_ring_rejects_delete_and_compact() {
    resetStorage();
    const unsigned int REC = 4;
    const unsigned long TSIZE = ringTableSize(REC, 3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.enableRingMode());
    TestRecord r = makeRecord(1);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC r));
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.deleteRec(1));
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.compact());
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.clear());
    TEST_ASSERT_EQUAL_UINT32(0, byteDb.count());
}

void test_ring_overwrites_when_full() {
    resetStorage();
    const unsigned int REC = 4;
    const unsigned long TSIZE = ringTableSize(REC, 3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.enableRingMode());
    for (int v = 1; v <= 4; v++) {
        TestRecord r = makeRecord(v * 10);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC r));
    }
    TEST_ASSERT_EQUAL_UINT32(3, byteDb.count());
    TestRecord at0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(1, EDB_REC at0));
    TEST_ASSERT_EQUAL_INT(40, at0.value);
}

void test_ring_fifo_iteration() {
    resetStorage();
    const unsigned int REC = 4;
    const unsigned long TSIZE = ringTableSize(REC, 3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.enableRingMode());
    for (int v = 1; v <= 4; v++) {
        TestRecord r = makeRecord(v * 10);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC r));
    }
    std::vector<int32_t> fifo;
    for (unsigned long r = byteDb.fifoFirstRec(); r != 0; r = byteDb.fifoNextRec(r)) {
        TestRecord rec;
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(r, EDB_REC rec));
        fifo.push_back(rec.value);
    }
    TEST_ASSERT_EQUAL_INT(3, (int)fifo.size());
    TEST_ASSERT_EQUAL_INT(20, fifo[0]);
    TEST_ASSERT_EQUAL_INT(30, fifo[1]);
    TEST_ASSERT_EQUAL_INT(40, fifo[2]);
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

// EDB_WRITE_IF_DIFFERENT: rewriting a slot with identical bytes must issue zero byte writes.
void test_write_if_different_skips_unchanged() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    TestRecord record = makeRecord(7);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));

    FakeStorage::instance().resetCounters();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.updateRec(1, EDB_REC record));  // identical payload
#if EDB_WRITE_IF_DIFFERENT
    TEST_ASSERT_EQUAL_UINT32(0, FakeStorage::instance().byte_writes);
#else
    TEST_ASSERT_TRUE(FakeStorage::instance().byte_writes > 0);
#endif
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(1, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(7, readBack.value);
}

void test_batch_append_persists_after_endBatch() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.beginBatch());
    TEST_ASSERT_TRUE(byteDb.batchActive());
    appendSequence(byteDb, 1, 5);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.endBatch());
    TEST_ASSERT_TRUE(!byteDb.batchActive());

    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    TEST_ASSERT_EQUAL_UINT32(5, reopened.count());
    assertLiveSequence(reopened, {1, 2, 3, 4, 5});
}

// Power loss mid-batch: no endBatch(). open() must recover every appended record.
void test_batch_reconciles_after_interrupted_batch() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.beginBatch());
    appendSequence(byteDb, 1, 4);
    // simulate a crash: drop the object without endBatch(), reopen from storage

    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    TEST_ASSERT_EQUAL_UINT32(4, reopened.count());
    assertLiveSequence(reopened, {1, 2, 3, 4});
    TEST_ASSERT_TRUE(!reopened.batchActive());

    // the dirty bit is cleared, so the table is fully usable again
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.deleteRec(2));
    TEST_ASSERT_EQUAL_UINT32(3, reopened.count());
    TestRecord more = makeRecord(9);
    unsigned long id = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.appendRec(EDB_REC more, &id));
    TEST_ASSERT_EQUAL_UINT32(2, id);   // freed slot reused -> free-list was rebuilt
}

void test_batch_rejects_mutations() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    appendSequence(byteDb, 1, 3);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.beginBatch());
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.deleteRec(1));
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.clear());
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.compact());
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.beginBatch());   // already batching
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.endBatch());
    TEST_ASSERT_EQUAL_INT(EDB_ERROR, byteDb.endBatch());     // not batching
}

// A batched append must cost far fewer byte writes than an unbatched one (no header publish).
void test_batch_reduces_writes() {
    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TABLE_SIZE, REC_SIZE));
    TestRecord record = makeRecord(1);

    FakeStorage::instance().resetCounters();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    unsigned long unbatched = FakeStorage::instance().byte_writes;

    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.beginBatch());
    FakeStorage::instance().resetCounters();
    TestRecord other = makeRecord(2);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC other));
    unsigned long batched = FakeStorage::instance().byte_writes;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.endBatch());

    TEST_ASSERT_TRUE(batched < unbatched);
}

// Consume the shipped dataset (test/data/sensorlog.bin, generated by tools/gen_datasets.py):
// store every record through EDB, read it back, and verify it survives byte-for-byte, plus a few
// values known from the generator. Data-driven and independently verifiable.
struct BenchSensorRec {
    uint32_t id;
    uint32_t ts;
    int16_t  temp_c100;
    uint16_t humidity;
    uint8_t  status;
    uint8_t  channel;
    char     tag[8];
} __attribute__((packed));

void test_dataset_roundtrip() {
    std::vector<uint8_t> raw;
    if (!loadFixtureFile("data/sensorlog.bin", raw)) {
        printf("SKIP data/sensorlog.bin not found (run tools/gen_datasets.py)\n");
        return;
    }
    const unsigned int rs = (unsigned int)sizeof(BenchSensorRec);   // 22
    TEST_ASSERT_EQUAL_UINT32(0, (uint32_t)(raw.size() % rs));
    unsigned long total = (unsigned long)(raw.size() / rs);
    unsigned long n = total < 500 ? total : 500;                    // a slice keeps the test fast

    resetStorage();
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, 256 + (n + 8) * (rs + 3), rs));
    for (unsigned long i = 0; i < n; i++)
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC raw[i * rs]));

    // every stored record must read back byte-for-byte
    for (unsigned long i = 0; i < n; i++) {
        uint8_t got[64];
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(i + 1, (EDB_Rec)got));
        TEST_ASSERT_EQUAL_UINT8_ARRAY(&raw[i * rs], got, (int)rs);
    }

    // values known from tools/gen_datasets.py: id = 1000 + index, channel = index % 8
    BenchSensorRec r0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(1, EDB_REC r0));
    TEST_ASSERT_EQUAL_UINT32(1000, r0.id);
    TEST_ASSERT_EQUAL_UINT8(0, r0.channel);
    TEST_ASSERT_EQUAL_UINT8_ARRAY("kitchen", r0.tag, 7);
    BenchSensorRec r10;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(10, EDB_REC r10));
    TEST_ASSERT_EQUAL_UINT32(1009, r10.id);
    TEST_ASSERT_EQUAL_UINT8(1, r10.channel);   // index 9 % 8
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
    RUN_TEST(test_stable_ids_assigned_on_append);
    RUN_TEST(test_ring_rejects_delete_and_compact);
    RUN_TEST(test_ring_overwrites_when_full);
    RUN_TEST(test_ring_fifo_iteration);
    RUN_TEST(test_buffer_delete_does_io);
    RUN_TEST(test_write_if_different_skips_unchanged);
    RUN_TEST(test_batch_append_persists_after_endBatch);
    RUN_TEST(test_batch_reconciles_after_interrupted_batch);
    RUN_TEST(test_batch_rejects_mutations);
    RUN_TEST(test_batch_reduces_writes);
    RUN_TEST(test_dataset_roundtrip);
    RUN_TEST(test_next_table_offset);
    RUN_TEST(test_open_or_create);
    RUN_TEST(test_multi_table_isolated_counts);
#endif
    return UNITY_END();
}
