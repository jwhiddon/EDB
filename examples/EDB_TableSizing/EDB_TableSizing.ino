/*
  EDB_TableSizing — compute table_size from max records (EDB v3)

  v3 layout per slot: status(1) + payload(rec_size) + crc16(2)  => slot_stride = rec_size + 3
  Record region starts at data_offset (96 bytes with redundant headers by default).

    table_size = data_offset + max_records * slot_stride
    limit()    == floor((table_size - data_offset) / slot_stride)

  RAM-backed storage — runs on any board.
*/
#include "Arduino.h"
#include <EDB.h>

struct Sample {
  int id;
  int reading;
};

static byte storage[1024];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

EDB db(&writer, &reader);

static uint16_t slotStride(unsigned int rec_size) {
  return (uint16_t)(1 + rec_size + 2);
}

static uint16_t dataOffset() {
#if EDB_HEADER_REDUNDANT
  return EDB_HEADER_SPAN;
#else
  return EDB_HEADER_COPY_SIZE;
#endif
}

static unsigned long tableSizeForMaxRecords(unsigned int rec_size, unsigned long max_records) {
  return (unsigned long)dataOffset() + max_records * slotStride(rec_size);
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("EDB table sizing demo\n");

  const unsigned long want_records = 10;
  const unsigned int rec_size = sizeof(Sample);
  const unsigned long table_size = tableSizeForMaxRecords(rec_size, want_records);

  Serial.print("rec_size=");
  Serial.print(rec_size);
  Serial.print("  slot_stride=");
  Serial.println(slotStride(rec_size));
  Serial.print("data_offset=");
  Serial.println(dataOffset());
  Serial.print("table_size for ");
  Serial.print(want_records);
  Serial.print(" records = ");
  Serial.println(table_size);

  if (db.create(0, table_size, rec_size) != EDB_OK) {
    Serial.println("create failed");
    return;
  }

  Serial.print("limit()=");
  Serial.print(db.limit());
  Serial.print("  (expected ");
  Serial.print(want_records);
  Serial.println(")");

  for (unsigned long i = 0; i < db.limit(); i++) {
    Sample s = {(int)(i + 1), (int)(i * 10)};
    if (db.appendRec(EDB_REC s) != EDB_OK) {
      Serial.println("unexpected append failure");
      return;
    }
  }
  if (db.appendRec(EDB_REC Sample{99, 99}) != EDB_TABLE_FULL) {
    Serial.println("expected EDB_TABLE_FULL on extra append");
  } else {
    Serial.println("extra append correctly returned EDB_TABLE_FULL");
  }

  Serial.print("count()=");
  Serial.println(db.count());
}

void loop() {}
