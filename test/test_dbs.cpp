#include "FakeStorage.h"
#include "test_dbs.h"

EDB byteDb(&FakeStorage::writeByte, &FakeStorage::readByte);
EDB bufferDb(&FakeStorage::writeBuffer, &FakeStorage::readBuffer);
EDB compatDb(&FakeStorage::writeByte, &FakeStorage::readByte);
