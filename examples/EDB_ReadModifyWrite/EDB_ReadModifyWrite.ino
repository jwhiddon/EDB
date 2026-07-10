/*
  EDB_ReadModifyWrite — partial field update via read-modify-write (EDB v3)

  updateRec() always writes the full record. To change one field, read the record into
  a struct, modify the field, then write it back.

  RAM-backed storage — runs on any board.
*/
#include "Arduino.h"
#include <EDB.h>

#define TABLE_SIZE 256

struct SensorReading {
  int id;
  int temperature;
};

static byte storage[TABLE_SIZE];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

EDB db(&writer, &reader);

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("EDB read-modify-write demo\n");

  if (db.openOrCreate(0, TABLE_SIZE, sizeof(SensorReading)) != EDB_OK) {
    Serial.println("table init failed");
    return;
  }

  SensorReading rec = {1, 68};
  unsigned long recno = 0;
  if (db.count() == 0) {
    db.appendRec(EDB_REC rec, &recno);
    Serial.print("appended recno ");
    Serial.print(recno);
    Serial.print(" temperature=");
    Serial.println(rec.temperature);
  } else {
    recno = db.firstRec();
  }

  // Read-modify-write: change only temperature.
  if (db.readRec(recno, EDB_REC rec) != EDB_OK) {
    Serial.println("readRec failed");
    return;
  }
  Serial.print("before update: temperature=");
  Serial.println(rec.temperature);

  rec.temperature = 72;
  if (db.updateRec(recno, EDB_REC rec) != EDB_OK) {
    Serial.println("updateRec failed");
    return;
  }

  if (db.readRec(recno, EDB_REC rec) != EDB_OK) {
    Serial.println("verify read failed");
    return;
  }
  Serial.print("after update:  temperature=");
  Serial.println(rec.temperature);
  Serial.println("\nPattern: readRec -> modify fields -> updateRec");
}

void loop() {}
