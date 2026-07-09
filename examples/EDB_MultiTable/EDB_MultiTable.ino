/*
  EDB_MultiTable.ino
  Two independent tables in the same EEPROM address space.

  head_ptr is a byte offset in storage, not a table number. Each table needs
  its own non-overlapping region: next_head = previous_head + previous_table_size.

  See docs/API.md section "Multiple tables".
*/

#include "Arduino.h"
#include <EDB.h>
#include <EEPROM.h>

#define EVENTS_TABLE_SIZE 256
#define CONFIG_TABLE_SIZE 128

#define EVENTS_HEAD 0
#define CONFIG_HEAD EDB::nextTableOffset(EVENTS_HEAD, EVENTS_TABLE_SIZE)

struct EventRecord {
  int id;
  int value;
};

struct ConfigRecord {
  int setting;
  int threshold;
};

EventRecord eventRecord;
ConfigRecord configRecord;

void writer(unsigned long address, byte data)
{
  EEPROM.write(address, data);
}

byte reader(unsigned long address)
{
  return EEPROM.read(address);
}

// Separate EDB objects keep their own headers after open/create.
EDB dbEvents(&writer, &reader);
EDB dbConfig(&writer, &reader);

void setup()
{
#if defined(ESP8266) || defined(ESP32)
  EEPROM.begin(EVENTS_TABLE_SIZE + CONFIG_TABLE_SIZE);
#endif

  Serial.begin(9600);
  Serial.println("EDB multi-table demo");
  Serial.println();

  if (dbEvents.openOrCreate(EVENTS_HEAD, EVENTS_TABLE_SIZE, sizeof(EventRecord)) != EDB_OK) {
    Serial.println("ERROR: events table init failed");
    return;
  }
  if (dbConfig.openOrCreate(CONFIG_HEAD, CONFIG_TABLE_SIZE, sizeof(ConfigRecord)) != EDB_OK) {
    Serial.println("ERROR: config table init failed");
    return;
  }

  Serial.print("Events head: ");
  Serial.print(dbEvents.headPtr());
  Serial.print(" count: ");
  Serial.println(dbEvents.count());

  Serial.print("Config head: ");
  Serial.print(dbConfig.headPtr());
  Serial.print(" count: ");
  Serial.println(dbConfig.count());

  if (dbEvents.count() == 0) {
    eventRecord.id = 1;
    eventRecord.value = 42;
    dbEvents.appendRec(EDB_REC eventRecord);
  }

  if (dbConfig.count() == 0) {
    configRecord.setting = 100;
    configRecord.threshold = 75;
    dbConfig.appendRec(EDB_REC configRecord);
  }

  dbEvents.readRec(1, EDB_REC eventRecord);
  dbConfig.readRec(1, EDB_REC configRecord);

  Serial.print("Event id=");
  Serial.print(eventRecord.id);
  Serial.print(" value=");
  Serial.println(eventRecord.value);

  Serial.print("Config setting=");
  Serial.print(configRecord.setting);
  Serial.print(" threshold=");
  Serial.println(configRecord.threshold);
}

void loop()
{
}
