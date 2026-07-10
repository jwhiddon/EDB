/*
  EDB_SchemaRotation — migrate to a new record layout (EDB v3)

  EDB has no in-place schema migration. To change record layout:
    1. Create a new table at the next free offset (nextTableOffset).
    2. Copy live records field-by-field into the new struct.
    3. Use the new table going forward (optionally clear the old one).

  RAM-backed storage — runs on any board.
*/
#include "Arduino.h"
#include <EDB.h>
#include <string.h>

#define MAX_RECORDS 6

struct ConfigV1 {
  int id;
  int threshold;
};

struct ConfigV2 {
  int id;
  int threshold;
  uint16_t flags;
  uint16_t reserved;
};

static byte storage[768];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

static const unsigned long TABLE_V1_HEAD = 0;
static const unsigned long TABLE_V1_SIZE =
    (unsigned long)EDB_HEADER_SPAN + MAX_RECORDS * (1 + sizeof(ConfigV1) + 2);
static const unsigned long TABLE_V2_HEAD = TABLE_V1_HEAD + TABLE_V1_SIZE;
static const unsigned long TABLE_V2_SIZE =
    (unsigned long)EDB_HEADER_SPAN + MAX_RECORDS * (1 + sizeof(ConfigV2) + 2);

EDB dbV1(&writer, &reader);
EDB dbV2(&writer, &reader);

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("EDB schema rotation demo\n");

  Serial.print("V1 table head=");
  Serial.print(TABLE_V1_HEAD);
  Serial.print(" size=");
  Serial.println(TABLE_V1_SIZE);
  Serial.print("V2 table head=");
  Serial.print(TABLE_V2_HEAD);
  Serial.print(" size=");
  Serial.println(TABLE_V2_SIZE);

  if (dbV1.openOrCreate(TABLE_V1_HEAD, TABLE_V1_SIZE, sizeof(ConfigV1)) != EDB_OK) {
    Serial.println("V1 init failed");
    return;
  }

  if (dbV1.count() == 0) {
    ConfigV1 rows[] = {{1, 50}, {2, 75}, {3, 90}};
    for (unsigned i = 0; i < 3; i++)
      dbV1.appendRec(EDB_REC rows[i]);
    Serial.println("seeded V1 table with 3 records");
  }

  if (dbV2.openOrCreate(TABLE_V2_HEAD, TABLE_V2_SIZE, sizeof(ConfigV2)) != EDB_OK) {
    Serial.println("V2 init failed");
    return;
  }

  if (dbV2.count() == 0) {
    Serial.println("copying live V1 records -> V2...");
    for (unsigned long recno = dbV1.firstRec(); recno != 0; recno = dbV1.nextRec(recno)) {
      ConfigV1 oldRec;
      if (dbV1.readRec(recno, EDB_REC oldRec) != EDB_OK) continue;
      ConfigV2 newRec;
      memset(&newRec, 0, sizeof(newRec));
      newRec.id = oldRec.id;
      newRec.threshold = oldRec.threshold;
      newRec.flags = 0x0001;  // new field default
      dbV2.appendRec(EDB_REC newRec);
      Serial.print("  V1 #");
      Serial.print(recno);
      Serial.print(" id=");
      Serial.print(oldRec.id);
      Serial.print(" -> V2 threshold=");
      Serial.println(newRec.threshold);
    }
  }

  Serial.println("\nV2 table after migration:");
  for (unsigned long recno = dbV2.firstRec(); recno != 0; recno = dbV2.nextRec(recno)) {
    ConfigV2 rec;
    dbV2.readRec(recno, EDB_REC rec);
    Serial.print("  recno ");
    Serial.print(recno);
    Serial.print(": id=");
    Serial.print(rec.id);
    Serial.print(" threshold=");
    Serial.print(rec.threshold);
    Serial.print(" flags=0x");
    Serial.println(rec.flags, HEX);
  }

  Serial.println("\nUse dbV2 for all new code; dbV1 can be cleared when no longer needed.");
}

void loop() {}
