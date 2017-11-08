/*
   EDB_SPIFFS.pde
   Extended Database Library + SPIFFS Flash FS Demo

   David Pye davidmpye@gmail.com

   The Extended Database library project page is here:
   http://www.arduino.cc/playground/Code/ExtendedDatabaseLibrary

*/

#include "Arduino.h"
#include <EDB.h>

//Use SPIFFS FS as data storage
#include <FS.h>

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

// The read and write handlers for using the SPIFFS Library
// Also blinks the led while writing/reading
inline void writer (unsigned long address, const byte* data, unsigned int recsize) {
    dbFile.seek(address, SeekSet);
    dbFile.write(data,recsize);
    dbFile.flush();
}

inline void reader (unsigned long address, byte* data, unsigned int recsize) {
    dbFile.seek(address, SeekSet);
    dbFile.read(data,recsize);
}

// Create an EDB object with the appropriate write and read handlers
EDB db(&writer, &reader);

// Run the demo
void setup()
{
    Serial.begin(9600);
    Serial.println(" Extended Database Library + SPIFFS storage demo");
    Serial.println();

    randomSeed(analogRead(0));

    SPIFFS.begin();
    delay(2000);

    if (SPIFFS.exists(db_name)) {

        dbFile = SPIFFS.open(db_name, "r+");

        if (dbFile) {
            Serial.print("Opening current table... ");
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
        dbFile = SPIFFS.open(db_name, "w+");
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
