/*
 EDB_24XX512.pde
 Extended Database Library + 24XX512 EEPROM Demo Sketch 
 
 The Extended Database library project page is here:
 http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary
 
 The E24C1024 library project page is here:
 http://www.arduino.cc/playground/Code/I2CEEPROM24C1024
 
 */
#include "Arduino.h"
#include <EDB.h>

// Use the 24XX512 EEPROM as storage
#include <Wire.h>
#include <E24C1024.h> // Should be compatible with 24XX512 series in this instance

// From the 24XX512 datasheet:
//
// The Chip Select bits A2, A1 and A0 can be used to expand the 
// contiguous address space for up to 4 Mbit by adding up to eight 
// 24XX512 devices on the same bus. 
//
// So, each device must be have their address pins wired as listed below to 
// create a single, contiguous address space across one or more devices.
//
// Example - 1 device: 
// Device connections: A0->GND, A1->GND, A2->GND
// Uncomment only the #define TABLE_SIZE 65536 line below.

// Example - 3 devices: 
// Device 1 connections: A0->GND, A1->GND, A2->GND
// Device 2 connections: A0->GND, A1->+5V, A2->GND
// Device 3 connections: A0->+5V, A1->+5V, A2->GND
// Uncomment only the #define TABLE_SIZE 196608 line below.
//
// Uncomment the ONE line appropriate for your platform.  
//#define TABLE_SIZE 65536 // 1 device: A0->GND, A1->GND, A2->GND
//#define TABLE_SIZE 131072 // 2 devices: A0->+5V, A1->1, A2->GND
//#define TABLE_SIZE 196608 // 3 devices: A0->GND, A1->+5V, A2->GND
//#define TABLE_SIZE 262144 // 4 devices: A0->+5V, A1->+5V, A2->GND
//#define TABLE_SIZE 327680 // 5 devices: A0->GND, A1->GND, A2->+5V
//#define TABLE_SIZE 393216 // 6 devices: A0->+5V, A1->GND, A2->+5V
//#define TABLE_SIZE 458752 // 7 devices: A0->GND, A1->+5V, A2->+5V
//#define TABLE_SIZE 524288 // 8 devices: A0->+5V, A1->+5V, A2->+5V

// default to the smallest - 1 device
#ifndef TABLE_SIZE
#define TABLE_SIZE 65536
#endif

// The number of demo records that should be created.  This should be less 
// than (TABLE_SIZE - sizeof(EDB_Header)) / sizeof(LogEvent).  If it is higher, 
// operations will return EDB_OUT_OF_RANGE for all records outside the usable range.
#define RECORDS_TO_CREATE 100

// Arbitrary record definition for this table.  
// This should be modified to reflect your record needs.
struct LogEvent {
  int id;
  int temperature;
} 
logEvent;

// Create an EDB object with the appropriate write and read handlers
EDB db(&E24C1024::write, &E24C1024::read);

// Run the demo
void setup()
{
  Serial.begin(9600);
  Serial.println("Extended Database Library + 24XX512 EEPROM Demo");

  randomSeed(analogRead(0));
  
  // create a table with starting address 0
  Serial.print("Creating table...");
  db.create(0, TABLE_SIZE, (unsigned int)sizeof(logEvent));
  Serial.println("DONE");

  recordLimit();
  countRecords();
  createRecords(RECORDS_TO_CREATE);
  countRecords();
  selectAll();
  countRecords();
  deleteAll();
  countRecords(); 
}

void loop()
{
}

void recordLimit()
{
  Serial.print("Record Limit: ");
  Serial.println(db.limit());
}

void deleteAll()
{
  Serial.print("Truncating table...");
  db.clear();
  Serial.println("DONE");
}

void countRecords()
{
  Serial.print("Record Count: "); 
  Serial.println(db.count());
}

void createRecords(int num_recs)
{
  Serial.print("Creating Records...");
  for (int recno = 1; recno <= num_recs; recno++)
  {
    logEvent.id = recno; 
    logEvent.temperature = random(1, 125);
    EDB_Status result = db.appendRec(EDB_REC logEvent);
    if (result != EDB_OK) printError(result);
  }
  Serial.println("DONE");
}

void selectAll()
{  
  for (int recno = 1; recno <= db.count(); recno++)
  {
    EDB_Status result = db.readRec(recno, EDB_REC logEvent);
    if (result == EDB_OK)
    {
      Serial.print("Recno: "); Serial.print(recno);
      Serial.print(" ID: "); Serial.print(logEvent.id);
      Serial.print(" Temp: "); Serial.println(logEvent.temperature);   
    }
    else printError(result);
  }
}

void printError(EDB_Status err)
{
  Serial.print("ERROR: ");
  switch (err)
  {
    case EDB_OUT_OF_RANGE:
      Serial.println("Recno out of range");
      break;
    case EDB_TABLE_FULL:
      Serial.println("Table full");
      break;
    case EDB_OK:
      Serial.println("OK");
      break;
    default:
      Serial.println("Unknown Error");
      break;
  }
}

