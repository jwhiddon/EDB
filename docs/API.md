# EDB API Reference

## Types

### `EDB_Status`

| Value | Meaning |
|-------|---------|
| `EDB_OK` | Success |
| `EDB_ERROR` | General failure (invalid header, I/O verification failed, malloc failure, v1 ESP32 write without migration) |
| `EDB_OUT_OF_RANGE` | `recno` is less than 1 or greater than `count()` |
| `EDB_TABLE_FULL` | Table cannot hold another record |

### `EDB_Rec`

Alias for `byte*`. Cast structs with the `EDB_REC` macro:

```cpp
MyRecord rec;
db.appendRec(EDB_REC rec);
```

## Constructors

```cpp
EDB(EDB_Write_Handler *write_byte, EDB_Read_Handler *read_byte);
EDB(EDB_Write_Buffer *write_buffer, EDB_Read_Buffer *read_buffer);
```

## `EDB_Status create(unsigned long head_ptr, unsigned long table_size, unsigned int rec_size)`

Creates a new v2 database at `head_ptr`.

- Sets `n_recs = 0`.
- Writes and read-verifies the header.
- Returns `EDB_ERROR` for invalid parameters or failed verification.

## `EDB_Status open(unsigned long head_ptr)`

Opens an existing database.

- Detects v2 or legacy v1 headers.
- Validates flag, sizes, and `n_recs <= limit()`.
- Returns `EDB_ERROR` for corrupt or unrecognized headers.

## `EDB_Status appendRec(const EDB_Rec rec)`

Appends a record at the end. Fastest insert path. Returns `EDB_TABLE_FULL` when full.

## `EDB_Status readRec(unsigned long recno, EDB_Rec rec)`

Reads record `recno` into caller-provided memory (`rec_size` bytes). Returns `EDB_OUT_OF_RANGE` when invalid.

## `EDB_Status updateRec(unsigned long recno, const EDB_Rec rec)`

Overwrites an existing record. Returns `EDB_OUT_OF_RANGE` or `EDB_ERROR`.

## `EDB_Status insertRec(unsigned long recno, const EDB_Rec rec)`

Inserts before the record currently at `recno`, shifting later records up. Slow for large tables. On an empty table, only `recno == 1` is valid.

## `EDB_Status deleteRec(unsigned long recno)`

Deletes a record and shifts later records down. Slow for large tables.

## `EDB_Status clear()`

Re-reads the header and recreates the table with the same `table_size` and `rec_size`, resetting `count()` to 0. Upgrades storage to v2.

This is a **destructive logical wipe**: it does not securely erase old record bytes from EEPROM/flash/SD — stale data may remain until overwritten. It is not a substitute for v1→v2 migration when you need to preserve records (use [MIGRATION.md](MIGRATION.md) instead).

## `unsigned long count()`

Returns the number of stored records.

## `unsigned long limit()`

Returns the maximum number of records that fit in the table. Returns `0` if `rec_size == 0`.

## `unsigned long headPtr() const`

Returns the byte offset in storage where this table's header begins (the value passed to `open()` or `create()`).

## `unsigned long tableSize() const`

Returns the allocated table size in bytes for the currently open table.

## `static unsigned long nextTableOffset(unsigned long head_ptr, unsigned long table_size)`

Returns the byte offset where the next table can start without overlapping:

```cpp
#define TABLE_A_SIZE 512
#define TABLE_B_SIZE 256
const unsigned long TABLE_A_HEAD = 0;
const unsigned long TABLE_B_HEAD = EDB::nextTableOffset(TABLE_A_HEAD, TABLE_A_SIZE);
```

## `EDB_Status openOrCreate(unsigned long head_ptr, unsigned long table_size, unsigned int rec_size)`

Calls `open(head_ptr)` and, if that returns `EDB_ERROR`, calls `create()` with the same parameters. Useful when initializing several tables at boot.

## Multiple tables

`head_ptr` is a **byte address** in EEPROM, SD, or other storage — not a table index. Do not use `create(1, ...)`, `create(2, ...)` unless those addresses are intentionally spaced.

### Recommended: one `EDB` object per table

Each instance keeps its own header in memory after `open()` or `create()`:

```cpp
EDB dbEvents(&writer, &reader);
EDB dbConfig(&writer, &reader);

dbEvents.openOrCreate(EVENTS_HEAD, EVENTS_TABLE_SIZE, sizeof(EventRecord));
dbConfig.openOrCreate(CONFIG_HEAD, CONFIG_TABLE_SIZE, sizeof(ConfigRecord));

dbEvents.appendRec(...);   // count() applies to events only
dbConfig.appendRec(...);   // count() applies to config only
```

### Alternative: one `EDB` object, switch with `open()`

```cpp
EDB db(&writer, &reader);
db.open(EVENTS_HEAD);
db.appendRec(...);
db.open(CONFIG_HEAD);
db.appendRec(...);
```

You must call `open()` before operating on each table. `count()` always reflects the **currently open** table only.

### Legacy single global `EDB edb`

1.0.x headers declared `extern EDB edb`. Sketches that define `EDB edb(&writer, &reader);` continue to work. For multiple tables, use separate `EDB` instances (or call `open()` before each table). Opt out of the legacy declaration in 2.0.0 with `#define EDB_NO_GLOBAL` before `#include <EDB.h>`.

## Record numbering rules

- Valid `recno` range for read/update/delete: `1` through `count()`.
- `recno == 0` always returns `EDB_OUT_OF_RANGE`.
- For insert on a non-empty table: `1` through `count()`.
