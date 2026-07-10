/*
  EDB_RecordTypes — safe POD record layouts (EDB v3)

  Records are fixed-size byte blobs. Use:
    - Plain structs with fixed-width types (uint32_t, etc.)
    - Fixed char buffers: char name[16]  (OK)
  Do NOT store Arduino String or char* in records — pointers are meaningless after reboot.

  RAM-backed storage — runs on any board.
*/
#include "Arduino.h"
#include <EDB.h>
#include <string.h>

#define TABLE_SIZE 512

struct Geo {
  int16_t lat_e6;   // latitude * 1e6
  int16_t lon_e6;
};

struct EventRecord {
  uint32_t id;
  uint32_t timestamp;
  char name[16];    // fixed buffer — copy with strncpy, not String
  Geo location;
};

static byte storage[TABLE_SIZE];
void writer(unsigned long address, byte data) { storage[address] = data; }
byte reader(unsigned long address) { return storage[address]; }

EDB db(&writer, &reader);

void setup() {
  Serial.begin(9600);
  while (!Serial) {}
  Serial.println("EDB record types demo\n");

  Serial.print("sizeof(EventRecord)=");
  Serial.println(sizeof(EventRecord));

  if (db.openOrCreate(0, TABLE_SIZE, sizeof(EventRecord)) != EDB_OK) {
    Serial.println("table init failed");
    return;
  }

  EventRecord rec;
  memset(&rec, 0, sizeof(rec));
  rec.id = 1;
  rec.timestamp = 1700000000UL;
  strncpy(rec.name, "sensor-A", sizeof(rec.name) - 1);
  rec.location.lat_e6 = 3777;   // 37.77°
  rec.location.lon_e6 = -12242; // -122.42°

  unsigned long recno = 0;
  db.appendRec(EDB_REC rec, &recno);
  Serial.print("stored at recno ");
  Serial.println(recno);

  EventRecord out;
  memset(&out, 0, sizeof(out));
  if (db.readRec(recno, EDB_REC out) != EDB_OK) {
    Serial.println("read failed");
    return;
  }

  Serial.print("id=");
  Serial.print(out.id);
  Serial.print(" name=\"");
  Serial.print(out.name);
  Serial.print("\" lat=");
  Serial.print(out.location.lat_e6 / 100.0, 2);
  Serial.print(" lon=");
  Serial.println(out.location.lon_e6 / 100.0, 2);

  Serial.println("\nAvoid: String name;  char* label;  virtual methods in structs.");
}

void loop() {}
