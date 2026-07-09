#pragma once

#include <vector>
#include <fstream>
#include <cstring>

#include "EDB.h"
#include "FakeStorage.h"
#include "unity.h"

struct TestRecord {
    int32_t value;
};

inline TestRecord makeRecord(int32_t value) {
    TestRecord record;
    record.value = value;
    return record;
}

inline void resetStorage() {
    FakeStorage::instance().reset();
}

inline bool readAllRecords(EDB& db, unsigned long expected_count, std::vector<int32_t>& out) {
    out.clear();
    out.reserve(expected_count);
    for (unsigned long recno = 1; recno <= expected_count; recno++) {
        TestRecord record;
        if (db.readRec(recno, EDB_REC record) != EDB_OK) return false;
        out.push_back(record.value);
    }
    return true;
}

inline void assertAllRecords(EDB& db, const std::vector<int32_t>& expected) {
    std::vector<int32_t> actual;
    TEST_ASSERT_TRUE(readAllRecords(db, expected.size(), actual));
    TEST_ASSERT_EQUAL_INT((int)expected.size(), (int)actual.size());
    for (size_t i = 0; i < expected.size(); i++) {
        TEST_ASSERT_EQUAL_INT(expected[i], actual[i]);
    }
}

inline void assertRecordRegionEquals(
    const std::vector<uint8_t>& a,
    const std::vector<uint8_t>& b) {
    TEST_ASSERT_EQUAL_INT((int)a.size(), (int)b.size());
    if (!a.empty()) {
        TEST_ASSERT_EQUAL_UINT8_ARRAY(a.data(), b.data(), a.size());
    }
}

inline void appendSequence(EDB& db, int start, int end) {
    for (int i = start; i <= end; i++) {
        TestRecord record = makeRecord(i);
        TEST_ASSERT_EQUAL_INT(EDB_OK, db.appendRec(EDB_REC record));
    }
}

inline bool loadFixtureFile(const char* path, std::vector<uint8_t>& out) {
    std::ifstream input(path, std::ios::binary);
    if (!input) return false;
    input.seekg(0, std::ios::end);
    std::streamsize size = input.tellg();
    input.seekg(0, std::ios::beg);
    out.resize((size_t)size);
    return (bool)input.read(reinterpret_cast<char*>(out.data()), size);
}

#ifdef EDB_VERSION
inline unsigned int v2HeaderSize() { return EDB_HEADER_V2_SIZE; }
inline bool isV2Release() { return true; }
#else
inline unsigned int v2HeaderSize() { return (unsigned int)sizeof(EDB_Header); }
inline bool isV2Release() { return false; }
#endif

inline void seedV1AvrDatabase(unsigned long n_recs, unsigned int rec_size, unsigned long table_size) {
    FakeStorage::instance().resize(table_size);
    std::vector<uint8_t>& data = FakeStorage::instance().data;
    data.assign(table_size, 0xFF);
    data[0] = EDB_FLAG;
    memcpy(&data[4], &n_recs, sizeof(n_recs));
    uint16_t rs = rec_size;
    memcpy(&data[8], &rs, sizeof(rs));
    uint16_t ts = (uint16_t)table_size;
    memcpy(&data[10], &ts, sizeof(ts));
    for (unsigned long i = 0; i < n_recs; i++) {
        int32_t value = (int32_t)(i + 1);
        memcpy(&data[12 + i * rec_size], &value, sizeof(value));
    }
}

inline void seedV1Esp32Database(unsigned long n_recs, unsigned int rec_size, unsigned long table_size) {
    FakeStorage::instance().resize(table_size);
    std::vector<uint8_t>& data = FakeStorage::instance().data;
    data.assign(table_size, 0xFF);
    data[0] = EDB_FLAG;
    memcpy(&data[4], &n_recs, sizeof(n_recs));
    uint16_t rs = rec_size;
    memcpy(&data[8], &rs, sizeof(rs));
    memcpy(&data[12], &table_size, sizeof(table_size));
    for (unsigned long i = 0; i < n_recs; i++) {
        int32_t value = (int32_t)(i + 1);
        memcpy(&data[16 + i * rec_size], &value, sizeof(value));
    }
}

inline std::vector<uint8_t> snapshotRecordRegion(
    unsigned long head_ptr,
    unsigned int header_size,
    unsigned long n_recs,
    unsigned int rec_size) {
    return FakeStorage::recordRegion(
        FakeStorage::instance().snapshot(),
        head_ptr,
        header_size,
        n_recs,
        rec_size);
}
