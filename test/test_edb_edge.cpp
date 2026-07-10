#include "TestHelpers.h"
#include "test_dbs.h"
#include "unity.h"

#include <cstring>
#include <vector>

#if EDB_VERSION

static unsigned long tableSizeForRecords(unsigned int rec_size, unsigned long max_records) {
    return EDB_HEADER_SPAN + max_records * (1 + rec_size + 2);
}

void test_fill_thousands_of_records() {
    resetStorage();
    const unsigned int REC = 4;
    const unsigned long N = 4096;
    const unsigned long TSIZE = tableSizeForRecords(REC, N);
    FakeStorage::instance().resize(TSIZE);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    TEST_ASSERT_EQUAL_UINT32(N, byteDb.limit());
    for (unsigned long i = 0; i < N; i++) {
        TestRecord record = makeRecord((int32_t)(i + 1));
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    TEST_ASSERT_EQUAL_UINT32(N, byteDb.count());

    TestRecord first, last, mid;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(1, EDB_REC first));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(N, EDB_REC last));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(N / 2, EDB_REC mid));
    TEST_ASSERT_EQUAL_INT(1, first.value);
    TEST_ASSERT_EQUAL_INT((int32_t)N, last.value);
    TEST_ASSERT_EQUAL_INT((int32_t)(N / 2), mid.value);

    unsigned long visited = 0;
    for (unsigned long r = byteDb.firstRec(); r != 0; r = byteDb.nextRec(r))
        visited++;
    TEST_ASSERT_EQUAL_UINT32(N, visited);
}

void test_sparse_table_many_tombstones() {
    resetStorage();
    const unsigned int REC = 4;
    const unsigned long N = 2048;
    const unsigned long TSIZE = tableSizeForRecords(REC, N);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    for (unsigned long i = 0; i < N; i++) {
        TestRecord record = makeRecord((int32_t)(i + 1));
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    for (unsigned long r = 2; r <= N; r += 2)
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.deleteRec(r));
    TEST_ASSERT_EQUAL_UINT32(N / 2, byteDb.count());

    unsigned long visited = 0;
    for (unsigned long r = byteDb.firstRec(); r != 0; r = byteDb.nextRec(r))
        visited++;
    TEST_ASSERT_EQUAL_UINT32(N / 2, visited);

    TestRecord reused = makeRecord(99999);
    unsigned long slot = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC reused, &slot));
    TEST_ASSERT_TRUE(slot % 2 == 0);
    TEST_ASSERT_EQUAL_UINT32(N / 2 + 1, byteDb.count());
}

struct LargePayload {
    int32_t key;
    uint8_t pad[252];
};

void test_large_record_size() {
    resetStorage();
    const unsigned int REC = (unsigned int)sizeof(LargePayload);
    const unsigned long N = 64;
    const unsigned long TSIZE = tableSizeForRecords(REC, N);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    for (int i = 0; i < 32; i++) {
        LargePayload record;
        memset(&record, 0, sizeof(record));
        record.key = i * 3;
        record.pad[0] = (uint8_t)i;
        record.pad[251] = (uint8_t)(255 - i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    TEST_ASSERT_EQUAL_UINT32(32, byteDb.count());
    LargePayload readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(16, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(45, readBack.key);
    TEST_ASSERT_EQUAL_UINT8(15, readBack.pad[0]);
    TEST_ASSERT_EQUAL_UINT8(240, readBack.pad[251]);
}

void test_high_head_ptr_offset() {
    resetStorage();
    const unsigned long HEAD = 131072;
    const unsigned int REC = 4;
    const unsigned long N = 512;
    const unsigned long TSIZE = tableSizeForRecords(REC, N);
    const unsigned long TOTAL = HEAD + TSIZE;
    FakeStorage::instance().resize(TOTAL);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(HEAD, TSIZE, REC));
    for (int i = 1; i <= 100; i++) {
        TestRecord record = makeRecord(i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(HEAD));
    TEST_ASSERT_EQUAL_UINT32(100, reopened.count());
    TestRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.readRec(50, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(50, readBack.value);
}

void test_ring_wrap_at_scale() {
    resetStorage();
    const unsigned int REC = 4;
    const unsigned long CAP = 256;
    const unsigned long TSIZE = tableSizeForRecords(REC, CAP);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.enableRingMode());
    const unsigned long APPENDS = CAP * 3;
    for (unsigned long i = 1; i <= APPENDS; i++) {
        TestRecord record = makeRecord((int32_t)i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    TEST_ASSERT_EQUAL_UINT32(CAP, byteDb.count());

    std::vector<int32_t> fifo;
    for (unsigned long r = byteDb.fifoFirstRec(); r != 0; r = byteDb.fifoNextRec(r)) {
        TestRecord rec;
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(r, EDB_REC rec));
        fifo.push_back(rec.value);
    }
    TEST_ASSERT_EQUAL_INT((int)CAP, (int)fifo.size());
    TEST_ASSERT_EQUAL_INT((int32_t)(APPENDS - CAP + 1), fifo[0]);
    TEST_ASSERT_EQUAL_INT((int32_t)APPENDS, fifo[fifo.size() - 1]);
}

struct IdRecord {
    uint32_t id;
    int32_t value;
};

void test_stable_ids_find_at_scale() {
    resetStorage();
    const unsigned int REC = (unsigned int)sizeof(IdRecord);
    const unsigned long N = 1000;
    const unsigned long TSIZE = tableSizeForRecords(REC, N);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.enableStableIds());
    for (int i = 0; i < 500; i++) {
        IdRecord record = {0, i};
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    unsigned long target_slot = 0;
    uint32_t target_id = 0;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.recordId(500, &target_id));
    TEST_ASSERT_EQUAL_UINT32(500, target_id);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.findRecById(target_id, &target_slot));
    TEST_ASSERT_EQUAL_UINT32(500, target_slot);
    IdRecord readBack;
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.readRec(target_slot, EDB_REC readBack));
    TEST_ASSERT_EQUAL_INT(499, readBack.value);
}

void test_megabyte_table_capacity() {
    resetStorage();
    const unsigned int REC = 4;
    const unsigned long TSIZE = 1024UL * 1024UL;
    FakeStorage::instance().resize(TSIZE);
    TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.create(0, TSIZE, REC));
    unsigned long limit = byteDb.limit();
    TEST_ASSERT_TRUE(limit > 100000);

    for (unsigned long i = 0; i < 5000; i++) {
        TestRecord record = makeRecord((int32_t)(i + 1));
        TEST_ASSERT_EQUAL_INT(EDB_OK, byteDb.appendRec(EDB_REC record));
    }
    TEST_ASSERT_EQUAL_UINT32(5000, byteDb.count());
    EDB reopened(&FakeStorage::writeByte, &FakeStorage::readByte);
    TEST_ASSERT_EQUAL_INT(EDB_OK, reopened.open(0));
    TEST_ASSERT_EQUAL_UINT32(5000, reopened.count());
}

int run_edge_tests() {
    UNITY_BEGIN();
    RUN_TEST(test_fill_thousands_of_records);
    RUN_TEST(test_sparse_table_many_tombstones);
    RUN_TEST(test_large_record_size);
    RUN_TEST(test_high_head_ptr_offset);
    RUN_TEST(test_ring_wrap_at_scale);
    RUN_TEST(test_stable_ids_find_at_scale);
    RUN_TEST(test_megabyte_table_capacity);
    return UNITY_END();
}

#else

int run_edge_tests() {
    return 0;
}

#endif
