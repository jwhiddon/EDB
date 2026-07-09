/*
  EDB_Migration — detecting a legacy file (EDB v3)

  v3 does not read v1/v2 files in place: open() returns EDB_NEEDS_MIGRATION for them. Convert the
  file to v3 offline with tools/edb_migrate.py, then copy it back. This sketch fakes a legacy v2
  header to show the detection, then creates a fresh v3 table that opens normally.

  Uses a RAM array as storage so it runs on any board.
*/
#include "Arduino.h"
#include <EDB.h>

#define TABLE_SIZE 128

struct Rec {
  int value;
};

static byte storage[TABLE_SIZE];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

EDB db(&writer, &reader);

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("EDB v3 legacy-detection demo\n");

  // Simulate a legacy v2 database: magic 0xDB, version byte 2.
  for (unsigned i = 0; i < TABLE_SIZE; i++) storage[i] = 0xFF;
  storage[0] = EDB_FLAG;
  storage[1] = 2;

  EDB_Status status = db.open(0);
  if (status == EDB_NEEDS_MIGRATION) {
    Serial.println("open() returned EDB_NEEDS_MIGRATION.");
    Serial.println("This is a legacy v1/v2 file. Convert it on a computer:");
    Serial.println("    python tools/edb_migrate.py old.db new.db");
    Serial.println("then copy new.db back to the device and open() again.");
  } else {
    Serial.print("unexpected status ");
    Serial.println((int)status);
  }

  // A freshly created v3 table opens normally.
  db.create(0, TABLE_SIZE, sizeof(Rec));
  Serial.print("\nafter create(), open() status=");
  Serial.print((int)db.open(0));
  Serial.println("  (0 == EDB_OK)");
}

void loop() {}
