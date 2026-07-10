/*
  EDB_Iteration — slot addressing, tombstone delete, and sparse iteration (EDB v3)

  recno is a 1-based slot index for read/update/delete — not a durable logical record id.
  deleteRec() tombstones the slot (O(1)); other slots keep their index. A later appendRec
  may reuse a freed slot with new data. Use rec.id (or enableStableIds()) for durable identity.
  Iterate live records with firstRec()/nextRec() instead of looping 1..count().

  RAM-backed storage — runs on any board.
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
  Serial.println("EDB v3 slot iteration demo\n");

  if (db.create(0, TABLE_SIZE, sizeof(Rec)) != EDB_OK) {
    Serial.println("create failed");
    return;
  }

  // appendRec(rec, &recno) returns the slot index for immediate I/O.
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

  // Delete two records. Other slot indices unchanged; table becomes sparse.
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

  // appendRec reuses a freed slot; same recno may hold different data afterward.
  Rec more = {99, 999};
  unsigned long reused = 0;
  db.appendRec(EDB_REC more, &reused);
  Serial.print("\nappend reused slot recno ");
  Serial.println(reused);
  printLive("after reuse:");

  // compact() reconciles the live count and rebuilds the free list.
  Serial.println("\ncompact()");
  db.compact();
  printLive("after compact:");
}

void loop() {}
