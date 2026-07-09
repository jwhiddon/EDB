/*
  EDB_Iteration — stable slot IDs, tombstone delete, and sparse iteration (EDB v3)

  In v3 a record's recno is a STABLE slot id: it never renumbers. deleteRec() tombstones the slot
  (O(1)); the id is not reused until a later append fills the gap. Because ids can be sparse after
  deletes, iterate live records with firstRec()/nextRec() instead of looping 1..count().

  This sketch uses a plain RAM array as storage so it runs on any board with no extra hardware.
*/
#include "Arduino.h"
#include <EDB.h>

#define TABLE_SIZE 512

struct Rec {
  int id;
  int value;
};

static byte storage[TABLE_SIZE];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

EDB db(&writer, &reader);

void printLive(const char *label) {
  Serial.print(label);
  Serial.print("  count=");
  Serial.print(db.count());
  Serial.print("  live: [");
  for (unsigned long recno = db.firstRec(); recno != 0; recno = db.nextRec(recno)) {
    Rec rec;
    db.readRec(recno, EDB_REC rec);
    Serial.print(" #");
    Serial.print(recno);
    Serial.print("=");
    Serial.print(rec.value);
  }
  Serial.println(" ]");
}

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("EDB v3 stable-slot iteration demo\n");

  if (db.create(0, TABLE_SIZE, sizeof(Rec)) != EDB_OK) {
    Serial.println("create failed");
    return;
  }

  // appendRec(rec, &recno) returns the stable id assigned to the record.
  for (int i = 1; i <= 5; i++) {
    Rec rec = {i, i * 10};
    unsigned long recno = 0;
    db.appendRec(EDB_REC rec, &recno);
    Serial.print("appended value ");
    Serial.print(rec.value);
    Serial.print(" -> recno ");
    Serial.println(recno);
  }
  printLive("after appends:");

  // Delete two records. Other records keep their ids; the slots become sparse.
  Serial.println("\ndelete recno 2 and 4");
  db.deleteRec(2);
  db.deleteRec(4);
  Serial.print("isLive(2)=");
  Serial.print(db.isLive(2));
  Serial.print("  isLive(3)=");
  Serial.println(db.isLive(3));
  Rec tmp;
  Serial.print("readRec(2) status=");
  Serial.print((int)db.readRec(2, EDB_REC tmp));   // 4 == EDB_DELETED
  Serial.println("  (EDB_DELETED)");
  printLive("after deletes:");

  // A new append reuses one of the freed slots, so the id comes back into use.
  Rec more = {99, 999};
  unsigned long reused = 0;
  db.appendRec(EDB_REC more, &reused);
  Serial.print("\nappend reused recno ");
  Serial.println(reused);
  printLive("after reuse:");

  // compact() reconciles the live count and rebuilds the free list (ids stay stable).
  Serial.println("\ncompact()");
  db.compact();
  printLive("after compact:");
}

void loop() {}
