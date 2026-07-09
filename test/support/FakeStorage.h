#pragma once

#include <vector>
#include <cstring>
#include <cstdint>
#include <functional>

class FakeStorage {
public:
    std::vector<uint8_t> data;
    unsigned long buffer_reads = 0;
    unsigned long buffer_writes = 0;

    void reset() {
        data.clear();
        buffer_reads = 0;
        buffer_writes = 0;
    }

    void load(const std::vector<uint8_t>& bytes) {
        data = bytes;
        buffer_reads = 0;
        buffer_writes = 0;
    }

    std::vector<uint8_t> snapshot() const {
        return data;
    }

    static std::vector<uint8_t> recordRegion(
        const std::vector<uint8_t>& storage,
        unsigned long head_ptr,
        unsigned int header_size,
        unsigned long n_recs,
        unsigned int rec_size) {
        unsigned long start = head_ptr + header_size;
        unsigned long end = start + (n_recs * rec_size);
        if (end > storage.size()) return std::vector<uint8_t>();
        return std::vector<uint8_t>(storage.begin() + start, storage.begin() + end);
    }

    void resize(unsigned long size) {
        if (data.size() < size) {
            data.resize(size, 0xFF);
        }
    }

    static FakeStorage& instance() {
        return active();
    }

    static FakeStorage& active() {
        return *activeStorage();
    }

    static void usePrimary() {
        activeStorage() = &primary();
    }

    static void useSecondary() {
        activeStorage() = &secondary();
    }

    static void writeByte(unsigned long address, uint8_t value) {
        instance().writeByteImpl(address, value);
    }

    static uint8_t readByte(unsigned long address) {
        return instance().readByteImpl(address);
    }

    static void writeBuffer(unsigned long address, const uint8_t* buffer, unsigned int size) {
        instance().writeBufferImpl(address, buffer, size);
    }

    static void readBuffer(unsigned long address, uint8_t* buffer, unsigned int size) {
        instance().readBufferImpl(address, buffer, size);
    }

private:
    static FakeStorage& primary() {
        static FakeStorage storage;
        return storage;
    }

    static FakeStorage& secondary() {
        static FakeStorage storage;
        return storage;
    }

    static FakeStorage*& activeStorage() {
        static FakeStorage* current = &primary();
        return current;
    }

    void writeByteImpl(unsigned long address, uint8_t value) {
        resize(address + 1);
        data[address] = value;
    }

    uint8_t readByteImpl(unsigned long address) {
        resize(address + 1);
        return data[address];
    }

    void writeBufferImpl(unsigned long address, const uint8_t* buffer, unsigned int size) {
        buffer_writes++;
        resize(address + size);
        std::memcpy(data.data() + address, buffer, size);
    }

    void readBufferImpl(unsigned long address, uint8_t* buffer, unsigned int size) {
        buffer_reads++;
        resize(address + size);
        std::memcpy(buffer, data.data() + address, size);
    }
};
