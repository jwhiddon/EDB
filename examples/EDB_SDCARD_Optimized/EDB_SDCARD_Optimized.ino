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
#if defined(ESP32)
#include <FS.h>
#endif

#define SD_PIN 10  // SD Card CS pin
#define TABLE_SIZE 8192

// The number of demo records that should be created.  This should be less
// than (TABLE_SIZE - sizeof(EDB_Header)) / sizeof(LogEvent).  If it is higher,
// operations will return EDB_OUT_OF_RANGE for all records outside the usable range.
#define RECORDS_TO_CREATE 10

static const char DB_PATH_DEFAULT[] = "/db/edb_test.db";
static const char DB_PATH_ROOT[] = "/edb_test.db";
const char* db_path = DB_PATH_DEFAULT;
File dbFile;

// FILE_WRITE includes O_APPEND, which breaks seek() on AVR SD (see arduino-libraries/SD#50).
#if defined(ESP32)
File openDbFileExisting() { return SD.open(db_path, "r+"); }
File openDbFileNew() { return SD.open(db_path, "w+"); }
#else
#define EDB_SD_OPEN_FLAGS (O_READ | O_WRITE | O_CREAT)
File openDbFileExisting() { return SD.open(db_path, EDB_SD_OPEN_FLAGS); }
File openDbFileNew() { return SD.open(db_path, EDB_SD_OPEN_FLAGS); }
#endif

void printError(EDB_Status err);

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
#if defined(ESP32)
    dbFile.seek(address, SeekSet);
#else
    dbFile.seek(address);
#endif
    dbFile.write(data,recsize);
    digitalWrite(13, LOW);
}

inline void reader (unsigned long address, byte* data, unsigned int recsize) {
    digitalWrite(13, HIGH);
#if defined(ESP32)
    dbFile.seek(address, SeekSet);
#else
    dbFile.seek(address);
#endif
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

    // Ensure database directory exists. Some SD stacks cannot create subfolders;
    // fall back to the card root rather than failing silently.
    if (!SD.exists("/db")) {
        Serial.println("Dir for Db files does not exist, creating...");
        if (!SD.mkdir("/db")) {
            Serial.println("WARN: Could not create /db (subfolder may be unsupported).");
            Serial.println("Falling back to SD card root: /edb_test.db");
            db_path = DB_PATH_ROOT;
        }
    }

    if (SD.exists(db_path)) {

        dbFile = openDbFileExisting();

        // Sometimes it wont open at first attempt, espessialy after cold start
        // Let's try one more time
        if (!dbFile) {
            dbFile = openDbFileExisting();
        }

        if (dbFile) {
            Serial.print("Openning current table... ");
            EDB_Status result = db.open(0);
            if (result == EDB_OK) {
                Serial.println("DONE");
            } else {
                Serial.println("ERROR");
                Serial.println("Did not find database in the file " + String(db_path));
                Serial.print("Creating new table... ");
                EDB_Status createResult = db.create(0, TABLE_SIZE, (unsigned int)sizeof(logEvent));
                if (createResult == EDB_OK) {
                    Serial.println("DONE");
                } else {
                    printError(createResult);
                    dbFile.close();
                    return;
                }
            }
        } else {
            Serial.println("Could not open file " + String(db_path));
            return;
        }
    } else {
        Serial.print("Creating table... ");
        dbFile = openDbFileNew();
        if (!dbFile) {
            dbFile = openDbFileNew();
        }
        if (!dbFile) {
            Serial.println("ERROR: Could not create file " + String(db_path));
            return;
        }
        EDB_Status createResult = db.create(0, TABLE_SIZE, (unsigned int)sizeof(logEvent));
        if (createResult == EDB_OK) {
            Serial.println("DONE");
        } else {
            printError(createResult);
            dbFile.close();
            return;
        }
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
    // v3: deleteRec() and insertRec() are O(1). deleteRec() tombstones a slot; a record's recno is
    // stable and never renumbers, and a later append reuses the freed slot. insertRec() also just
    // allocates a free slot (positional order is not preserved). Iterate with firstRec()/nextRec().
    createRecords(5);
    Serial.println("Deleting recno 3 (leaves a tombstone gap)...");
    deleteOneRecord(3);
    countRecords();
    selectAll();
    Serial.println("Appending reuses the freed slot 3...");
    insertOneRecord(1);
    selectAll();

    dbFile.flush();
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
    EDB_Status result = db.clear();
    if (result != EDB_OK) printError(result);
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
    for (unsigned long recno = db.firstRec(); recno != 0; recno = db.nextRec(recno))
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
        case EDB_ERROR:
            Serial.println("Database error");
            break;
        case EDB_OK:
        default:
            Serial.println("OK");
            break;
    }
}
