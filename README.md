# Arduino Extended Database Library

This Arduino Extended Database Library increases the maximum number of records allowed in a database from 256 records (byte) to a theoretical maximum of 4,294,967,295 records (unsigned long). The maximum record size was also increased from 256 bytes (byte) to 65,534 bytes (unsigned int).

You may use this library in conjunction with the standard Arduino EEPROM library, an external EEPROM such as the AT24C1024, or any other platform that supports byte level reading and writing such as an SD card.

[Extended Database Library project's home at the Arduino Playground](http://playground.arduino.cc/Code/ExtendedDatabaseLibrary)

## Credits

This is a re-implementation of the [Arduino Database Library](http://playground.arduino.cc/Code/DatabaseLibrary) originally written by Madhusudana das.

## Install

* Unzip the download into your Arduino libraries directory
* If the Arduino IDE is already running then exit and restart the Arduino IDE

## Getting Started

* Include `EDB.h` in your Arduino sketch
* Define the data structure for your records
* Include an I/O interface such as `EEPROM.h`
* Declare an instance of `EDB` in your Arduino sketch
* Pick a storage address at which the table should start
* Call `create()` on first use, then `open()` on subsequent runs

```cpp
#include <EDB.h>
#include <EEPROM.h>

struct LogEvent { int id; int temperature; } logEvent;

void writer(unsigned long address, byte data) { EEPROM.write(address, data); }
byte reader(unsigned long address) { return EEPROM.read(address); }

EDB db(&writer, &reader);

void setup() {
  if (db.open(0) != EDB_OK) {
    db.create(0, 512, sizeof(logEvent));
  }
  logEvent.id = 1;
  logEvent.temperature = 72;
  db.appendRec(EDB_REC logEvent);
}
```

## Documentation

* [Architecture](docs/ARCHITECTURE.md)
* [On-disk format](docs/FORMAT.md)
* [API reference](docs/API.md)
* [Upgrade guide](docs/UPGRADE.md) — **start here if upgrading from 1.0.6**
* [FAQ](docs/FAQ.md)
* [v1 to v2 migration](docs/MIGRATION.md)
* [Testing](docs/TESTING.md)
* [Migration tool](tools/README.md)
* [1.0.7 drop-in release](release/1.0.7/README.md)

## Examples

* [Simple Example using internal Arduino EEPROM](examples/EDB_Simple)
* [Multiple tables in one storage backend](examples/EDB_MultiTable)
* [Arduino EEPROM providing 4096 - 32768 bits of address space](examples/EDB_Internal_EEPROM)
* [AT24C1024 I2C EEPROM providing 1,048,576 bits of address space](examples/EDB_AT24C1024)
* [24XX512 EEPROM providing up to 4 Mbit of address space](examples/EDB_24XX512)
* [SD Card example](examples/EDB_SDCARD)
* [SD Card optimized example](examples/EDB_SDCARD_Optimized)
* [SPIFFS example](examples/EDB_SPIFFS)
* [SPIFFS optimized example](examples/EDB_SPIFFS_Optimized)

## Testing

```bash
make test
```

This runs native tests for **2.0.0** and **1.0.7**, migration tests, and API compatibility checks.

Existing users on 1.0.6 can adopt the drop-in [1.0.7 release](release/1.0.7/README.md) first — see [docs/UPGRADE.md](docs/UPGRADE.md).

See [docs/TESTING.md](docs/TESTING.md) for details.

## Migration from v1

EDB 2.0.0 uses a packed 12-byte cross-platform header. Migrate SD/SPIFFS database files with:

```bash
python tools/edb_migrate.py old.db new.db --arch auto
```

See [docs/MIGRATION.md](docs/MIGRATION.md).

## Releases

### 2.0.0

* Packed v2 on-disk header (12 bytes, cross-platform)
* Fixed `recno == 0` bounds checking across all operations
* `open()` and `create()` validate headers and verify writes
* `malloc` failure handling in shift operations
* Block-shift optimization for buffer handlers
* Removed stale mandatory reliance on a single global instance; `extern EDB edb` kept for 1.0.x compatibility
* Multi-table helpers: `headPtr()`, `tableSize()`, `nextTableOffset()`, `openOrCreate()`
* `clear()` returns `EDB_Status`
* Added migration tool, documentation, native test suite, and CI

### 1.0.7

* Drop-in safety fixes for 1.0.6 (same on-disk format and API)
* Bounds checks, header validation, malloc guards — see [release/1.0.7/README.md](release/1.0.7/README.md)

### 1.0.6 - Nov 2, 2017

* Added buffer read/write handlers for SPIFFS and SD cards
* Added optimized SPIFFS and SD card examples

### 1.0.0 - Feb 12, 2016

* Updated to support Arduino 1.0.0+
* Bug fix in `EDB::open`
* Moved project to GitHub

### 0.7.0 - Dec 8, 2009

* Initial release
