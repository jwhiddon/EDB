/*
  EDB_Integrity — per-record CRC and the redundant, crash-safe header (EDB v3)

  v3 stores two CRC32-checksummed copies of the header (so a torn header write can't corrupt the
  table) and a CRC16 on every record (so a torn/bit-rot record is reported as EDB_CORRUPT instead
  of returned as good data). This sketch deliberately damages storage to show both protections.

  Uses a RAM array as storage so it runs on any board.
*/
#include "Arduino.h"
#include <EDB.h>
#include <string.h>

#define TABLE_SIZE 512

struct Rec {
  int id;
  int value;
};

static byte storage[TABLE_SIZE];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

EDB db(&writer, &reader);

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("EDB v3 integrity demo (per-record CRC + redundant header)\n");

  if (db.create(0, TABLE_SIZE, sizeof(Rec)) != EDB_OK) {
    Serial.println("create failed");
    return;
  }
  for (int i = 1; i <= 3; i++) {
    Rec rec = {i, i * 100};
    db.appendRec(EDB_REC rec);
  }

  // 1) Per-record CRC detects corruption.
  Rec r;
  Serial.print("readRec(1) status=");
  Serial.println((int)db.readRec(1, EDB_REC r));   // 0 == EDB_OK

  // Record data begins at EDB_HEADER_SPAN; a slot is status(1) || payload || crc16(2).
  storage[EDB_HEADER_SPAN + 1] ^= 0xFF;            // flip a payload byte of record 1
  Serial.print("readRec(1) after flipping a byte: status=");
  Serial.print((int)db.readRec(1, EDB_REC r));
  Serial.println("  (5 == EDB_CORRUPT)");

  Rec fixed = {1, 100};                            // rewriting recomputes the CRC
  db.updateRec(1, EDB_REC fixed);
  Serial.print("readRec(1) after rewrite: status=");
  Serial.println((int)db.readRec(1, EDB_REC r));

  // 2) The redundant header survives a damaged copy.
  uint32_t seq0 = 0, seq1 = 0;
  memcpy(&seq0, &storage[4], 4);
  memcpy(&seq1, &storage[EDB_HEADER_COPY_SIZE + 4], 4);
  unsigned older = (seq0 <= seq1) ? 0 : 1;         // corrupt the stale copy, not the current one
  storage[older * EDB_HEADER_COPY_SIZE + 5] ^= 0xFF;

  EDB reopened(&writer, &reader);
  Serial.print("\nreopen after damaging the older header copy: status=");
  Serial.print((int)reopened.open(0));
  Serial.print("  count=");
  Serial.println(reopened.count());
  Serial.print("readRec(3) value=");
  reopened.readRec(3, EDB_REC r);
  Serial.println(r.value);
}

void loop() {}
