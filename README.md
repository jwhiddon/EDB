# Arduino Extended Database Library

This Arduino Extended Database Library increases the maximum number of records allowed in a database from 256 records (byte) to a theoretical maximum of 4,294,967,295 records (unsigned long). The maximum record size was also increased from 256 bytes (byte) to 65,534 bytes (unsigned int).

You may use this library in conjunction with the standard Arduino EEPROM library, an external EEPROM such as the AT24C1024, or any other platform that supports byte level reading and writing such as an SD card.

[Extended Database Library project's home at the Arduino Playground](http://playground.arduino.cc/Code/ExtendedDatabaseLibrary)

## Credits

This is a re-implementation of the [Arduino Database Library](http://playground.arduino.cc/Code/DatabaseLibrary) originally written by Madhusudana das.

## Thanks

Community reports and pull requests that shaped the 1.0.7 and 2.0.0 releases:

### Bug fixes and patches

| Contributor | Contribution |
|-------------|--------------|
| [@arkhipenko](https://github.com/arkhipenko) | [PR #43](https://github.com/jwhiddon/EDB/pull/43) — `limit()` division-by-zero guard and header validation on `open()` |
| [@M-4A](https://github.com/M-4A) | [#17](https://github.com/jwhiddon/EDB/issues/17) — `EDB_table_ptr` missing in `open()` |
| [@carloboy16](https://github.com/carloboy16) | [#29](https://github.com/jwhiddon/EDB/issues/29) — data corruption on `updateRec` |
| [@bitbronze](https://github.com/bitbronze) | [#39](https://github.com/jwhiddon/EDB/issues/39) — SD `FILE_WRITE` / `O_APPEND` breaks `seek()` |
| [@DavisDevasia](https://github.com/DavisDevasia) | [#20](https://github.com/jwhiddon/EDB/pull/20) — SD subfolder creation failures |
| [@ianwillianb](https://github.com/ianwillianb) | Comments on [#33](https://github.com/jwhiddon/EDB/issues/33) / [#36](https://github.com/jwhiddon/EDB/issues/36) — `r+` vs `FILE_APPEND` on SD |
| [@giapoldo](https://github.com/giapoldo) | [#33](https://github.com/jwhiddon/EDB/issues/33) — SDCARD_Optimized read returning wrong records |
| [@uvedhe](https://github.com/uvedhe) | [#35](https://github.com/jwhiddon/EDB/issues/35) — `db.open()` failures on SD |
| [@sheimend](https://github.com/sheimend) | [#36](https://github.com/jwhiddon/EDB/issues/36) — examples returning zeros on ESP32 |
| [@ktorimaru](https://github.com/ktorimaru) | [#16](https://github.com/jwhiddon/EDB/issues/16) — ESP8266 requires `EEPROM.begin()` |
| [@Bob2345de](https://github.com/Bob2345de) | [#32](https://github.com/jwhiddon/EDB/issues/32) — `char*` string constant warnings |

### Documentation and usage questions

| Contributor | Contribution |
|-------------|--------------|
| [@teimouri](https://github.com/teimouri) | [#38](https://github.com/jwhiddon/EDB/issues/38) — multiple tables / `head_ptr` layout |
| [@alceuscardoso](https://github.com/alceuscardoso) | [#34](https://github.com/jwhiddon/EDB/issues/34) — multi-table `count()` confusion |
| [@nicobrix3](https://github.com/nicobrix3) | [#31](https://github.com/jwhiddon/EDB/issues/31) — multiple tables with `create()` |
| [@enriquecml](https://github.com/enriquecml) | [#26](https://github.com/jwhiddon/EDB/issues/26) — multiple database instances |
| [@GPFisher](https://github.com/GPFisher) | [#15](https://github.com/jwhiddon/EDB/issues/15) — strings in record structs |
| [@SerhioRed](https://github.com/SerhioRed) | [#22](https://github.com/jwhiddon/EDB/issues/22) — partial struct updates |
| [@darkpipo6](https://github.com/darkpipo6) | [#23](https://github.com/jwhiddon/EDB/issues/23) — `char` arrays in structs |
| [@wolkstein](https://github.com/wolkstein) | [#24](https://github.com/jwhiddon/EDB/issues/24) — changing record schema |
| [@ThePatrickMartin](https://github.com/ThePatrickMartin) | [#25](https://github.com/jwhiddon/EDB/issues/25) — nested struct support |
| [@arnolde](https://github.com/arnolde) | [#40](https://github.com/jwhiddon/EDB/issues/40) — SPIFFS insert performance |
| [@copercini](https://github.com/copercini) | [#18](https://github.com/jwhiddon/EDB/issues/18) — WDT resets in SPIFFS example |

Also thanks to [@DedeHai](https://github.com/DedeHai) for [PR #28](https://github.com/jwhiddon/EDB/pull/28) (buffer read/write handlers, 1.0.6) and community commenters who helped others in threads we closed with [docs/FAQ.md](docs/FAQ.md).

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
* [Gateway & Manager](docs/GATEWAY.md) — REST API and web UI for device databases
* [Encryption](docs/ENCRYPTION.md) — end-to-end and at-rest encryption
* [1.0.7 drop-in release](release/1.0.7/README.md)

## Gateway & Manager

A host-side FastAPI service and web UI proxy EDB on microcontrollers over serial. The gateway relays **ciphertext only** when encryption is enabled; decryption happens in your browser.

```bash
cd services/edb-gateway
pip install -e ".[dev]"
uvicorn edb_gateway.main:app --host 127.0.0.1 --port 8765
```

Open [http://127.0.0.1:8765/manager](http://127.0.0.1:8765/manager). See [docs/GATEWAY.md](docs/GATEWAY.md).

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
* [Serial bridge for gateway](examples/EDB_SerialBridge) — ESP32 + SD

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
