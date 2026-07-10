/*
  EDB_AppendOnlyLogger — ring FIFO time-series log (EDB v3)

  enableRingMode(): append-only, overwrites oldest when full (no tombstones).
  deleteRec is not allowed — use clear() to wipe the log (see deleteAll() below).

  Walk entries oldest-first with fifoFirstRec()/fifoNextRec(). Use a record id field in
  your struct for durable identity; recno is only a slot handle.

  RAM-backed storage — runs on any board.
*/
#include "Arduino.h"
#include <EDB.h>

#define MAX_LOG_RECORDS 8

struct LogEntry {
  uint32_t id;           // app-assigned or use enableStableIds()
  uint32_t millis_stamp;
  int value;
};

static byte storage[512];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

EDB db(&writer, &reader);

static unsigned long tableSizeForRecords(unsigned int rec_size, unsigned long max_records) {
  const uint16_t stride = (uint16_t)(1 + rec_size + 2);
  return (unsigned long)EDB_HEADER_SPAN + max_records * stride;
}

void deleteAll() {
  db.clear();
}

void printLog(const char *label) {
  Serial.print(label);
  Serial.print(" count=");
  Serial.print(db.count());
  Serial.print(" limit=");
  Serial.print(db.limit());
  Serial.print("  entries (FIFO):");
  for (unsigned long recno = db.fifoFirstRec(); recno != 0; recno = db.fifoNextRec(recno)) {
    LogEntry e;
    if (db.readRec(recno, EDB_REC e) == EDB_OK) {
      Serial.print(" [");
      Serial.print(e.value);
      Serial.print("@");
      Serial.print(e.millis_stamp);
      Serial.print("]");
    }
  }
  Serial.println();
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("EDB ring FIFO logger demo\n");

  const unsigned long table_size = tableSizeForRecords(sizeof(LogEntry), MAX_LOG_RECORDS);
  if (db.openOrCreate(0, table_size, sizeof(LogEntry)) != EDB_OK) {
    Serial.println("table init failed");
    return;
  }
  if (db.enableRingMode() != EDB_OK) {
    Serial.println("enableRingMode failed");
    return;
  }
  Serial.print("limit()=");
  Serial.println(db.limit());

  for (int i = 0; i < (int)MAX_LOG_RECORDS + 2; i++) {
    LogEntry e = {(uint32_t)(i + 1), (uint32_t)(i * 1000UL), 20 + i};
    if (db.appendRec(EDB_REC e) != EDB_OK) {
      Serial.println("append failed");
      break;
    }
  }
  printLog("after filling past limit (oldest overwritten):");
}

void loop() {
  static unsigned long last = 0;
  if (millis() - last < 2000) return;
  last = millis();

  static uint32_t next_id = 100;
  LogEntry e = {next_id++, millis(), (int)(analogRead(A0) & 0xFF)};
  if (db.appendRec(EDB_REC e) == EDB_OK) {
    Serial.print("logged value ");
    Serial.print(e.value);
    Serial.print(" count=");
    Serial.println(db.count());
  }
}
