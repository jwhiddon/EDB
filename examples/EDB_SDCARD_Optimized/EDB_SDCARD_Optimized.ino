/*
   EDB_SDCARD.pde
   Extended Database Library + External SD CARD storage demo

   Thanks to https://github.com/firebull/arduino-edb/ for the SD CARD example. 

   The Extended Database library project page is here:
   http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary

*/

#include "Arduino.h"
#include <EDB.h>

// Use the external SPI SD card as storage
#include <SPI.h>
#include <SD.h>

#define SD_PIN 10  // SD Card CS pin
#define TABLE_SIZE 8192

// The number of demo records that should be created.  This should be less
// than (TABLE_SIZE - sizeof(EDB_Header)) / sizeof(LogEvent).  If it is higher,
// operations will return EDB_OUT_OF_RANGE for all records outside the usable range.
#define RECORDS_TO_CREATE 10

char* db_name = "/db/edb_test.db";
File dbFile;

// Arbitrary record definition for this table.
// This should be modified to reflect your record needs.
struct LogEvent {
    int id;
    int temperature;
}
logEvent;

// The read and write handlers for using the SD Library
// Also blinks the led while writing/reading
inline void writer (unsigned long address, const byte* data, unsigned int recsize) {
    digitalWrite(13, HIGH);
    dbFile.seek(address);
    dbFile.write(data,recsize);
    dbFile.flush();
    digitalWrite(13, LOW);
}

inline void reader (unsigned long address, byte* data, unsigned int recsize) {
    digitalWrite(13, HIGH);
    dbFile.seek(address);
    dbFile.read(data,recsize);
    digitalWrite(13, LOW);
}

// Create an EDB object with the appropriate write and read handlers
EDB db(&writer, &reader);

// Run the demo
void setup()
{
    pinMode(13, OUTPUT);
    digitalWrite(13, LOW);

    Serial.begin(9600);
    Serial.println(" Extended Database Library + External SD CARD storage demo");
    Serial.println();

    randomSeed(analogRead(0));

    if (!SD.begin(SD_PIN)) {
        Serial.println("No SD-card.");
        return;
    }

    // Check dir for db files
    if (!SD.exists("/db")) {
        Serial.println("Dir for Db files does not exist, creating...");
        SD.mkdir("/db");
    }

    if (SD.exists(db_name)) {

        dbFile = SD.open(db_name, FILE_WRITE);

        // Sometimes it wont open at first attempt, espessialy after cold start
        // Let's try one more time
        if (!dbFile) {
            dbFile = SD.open(db_name, FILE_WRITE);
        }

        if (dbFile) {
            Serial.print("Openning current table... ");
            EDB_Status result = db.open(0);
            if (result == EDB_OK) {
                Serial.println("DONE");
            } else {
                Serial.println("ERROR");
                Serial.println("Did not find database in the file " + String(db_name));
                Serial.print("Creating new table... ");
                db.create(0, TABLE_SIZE, (unsigned int)sizeof(logEvent));
                Serial.println("DONE");
                return;
            }
        } else {
            Serial.println("Could not open file " + String(db_name));
            return;
        }
    } else {
        Serial.print("Creating table... ");
        // create table at with starting address 0
        dbFile = SD.open(db_name, FILE_WRITE);
        db.create(0, TABLE_SIZE, (unsigned int)sizeof(logEvent));
        Serial.println("DONE");
    }

    recordLimit();
    countRecords();
    createRecords(RECORDS_TO_CREATE);
    countRecords();
    selectAll();
    deleteOneRecord(RECORDS_TO_CREATE / 2);
    countRecords();
    selectAll();
    appendOneRecord(RECORDS_TO_CREATE + 1);
    countRecords();
    selectAll();
    insertOneRecord(RECORDS_TO_CREATE / 2);
    countRecords();
    selectAll();
    updateOneRecord(RECORDS_TO_CREATE);
    selectAll();
    countRecords();
    deleteAll();
    Serial.println("Use insertRec() and deleteRec() carefully, they can be slow");
    countRecords();
    for (int i = 1; i <= 20; i++) insertOneRecord(1);  // inserting from the beginning gets slower and slower
    countRecords();
    for (int i = 1; i <= 20; i++) deleteOneRecord(1);  // deleting records from the beginning is slower than from the end
    countRecords();

    dbFile.close();
}

void loop()
{
}

// utility functions

void recordLimit()
{
    Serial.print("Record Limit: ");
    Serial.println(db.limit());
}

void deleteOneRecord(int recno)
{
    Serial.print("Deleting recno: ");
    Serial.println(recno);
    db.deleteRec(recno);
}

void deleteAll()
{
    Serial.print("Truncating table... ");
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
    Serial.print("Creating Records... ");
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
            Serial.print("Recno: ");
            Serial.print(recno);
            Serial.print(" ID: ");
            Serial.print(logEvent.id);
            Serial.print(" Temp: ");
            Serial.println(logEvent.temperature);
        }
        else printError(result);
    }
}

void updateOneRecord(int recno)
{
    Serial.print("Updating record at recno: ");
    Serial.print(recno);
    Serial.print("... ");
    logEvent.id = 1234;
    logEvent.temperature = 4321;
    EDB_Status result = db.updateRec(recno, EDB_REC logEvent);
    if (result != EDB_OK) printError(result);
    Serial.println("DONE");
}

void insertOneRecord(int recno)
{
    Serial.print("Inserting record at recno: ");
    Serial.print(recno);
    Serial.print("... ");
    logEvent.id = recno;
    logEvent.temperature = random(1, 125);
    EDB_Status result = db.insertRec(recno, EDB_REC logEvent);
    if (result != EDB_OK) printError(result);
    Serial.println("DONE");
}

void appendOneRecord(int id)
{
    Serial.print("Appending record... ");
    logEvent.id = id;
    logEvent.temperature = random(1, 125);
    EDB_Status result = db.appendRec(EDB_REC logEvent);
    if (result != EDB_OK) printError(result);
    Serial.println("DONE");
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
        default:
            Serial.println("OK");
            break;
    }
}
