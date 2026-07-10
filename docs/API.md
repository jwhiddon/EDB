# EDB API Reference

## Types

### `EDB_Status`

| Value | Meaning |
|-------|---------|
| `EDB_OK` | Success |
| `EDB_ERROR` | General failure (invalid/corrupt header, I/O verification failed, invalid params) |
| `EDB_OUT_OF_RANGE` | `recno` is less than 1 or refers to a never-used slot |
| `EDB_TABLE_FULL` | Table cannot hold another record |
| `EDB_DELETED` | `recno` refers to a deleted (tombstoned) slot |
| `EDB_CORRUPT` | A record or header CRC failed verification |
| `EDB_NEEDS_MIGRATION` | A legacy v1/v2 file — migrate offline with `tools/edb_migrate.py` |

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

Creates a new v3 database at `head_ptr`.

- Sets `n_recs = 0`.
- Writes and read-verifies the header.
- Returns `EDB_ERROR` for invalid parameters or failed verification.

## `EDB_Status open(unsigned long head_ptr)`

Opens an existing database.

- Detects legacy v1/v2 headers.
- Validates flag, sizes, and `n_recs <= limit()`.
- Returns `EDB_ERROR` for corrupt or unrecognized headers.

## `EDB_Status appendRec(const EDB_Rec rec)` / `appendRec(const EDB_Rec rec, unsigned long* out_recno)`

Appends a record into a free slot (reused or newly grown), O(1). The optional `out_recno` receives
the **slot index** for immediate `readRec` / `updateRec` / `deleteRec` — not a durable logical id.
On normal tables, returns `EDB_TABLE_FULL` when full. On **ring** tables, when full the oldest slot
is overwritten in place and `EDB_OK` is returned (ring mode never returns `EDB_TABLE_FULL`).

## `EDB_Status readRec(unsigned long recno, EDB_Rec rec)`

Reads record `recno` into caller-provided memory (`rec_size` bytes) and verifies its CRC. Returns
`EDB_OUT_OF_RANGE` (never-used id), `EDB_DELETED` (tombstoned), or `EDB_CORRUPT` (CRC mismatch).

## `EDB_Status updateRec(unsigned long recno, const EDB_Rec rec)`

Overwrites an existing live record, O(1) (no header write). Returns `EDB_OUT_OF_RANGE`/`EDB_DELETED`.

## `EDB_Status insertRec(unsigned long recno, const EDB_Rec rec)`

**v3 behavior:** allocates a free slot like `appendRec`; the `recno` argument is ignored and
positional order is **not** preserved. Prefer `appendRec`. (Kept for source compatibility.)

## `EDB_Status deleteRec(unsigned long recno)`

Tombstones the record's slot, O(1). The slot may be reused by a later `appendRec` with different
data. Returns `EDB_ERROR` on **ring** tables (append-only; use `clear()` to wipe). Returns
`EDB_OUT_OF_RANGE`/`EDB_DELETED` for invalid ids on normal tables.

## `unsigned long firstRec()` / `unsigned long nextRec(unsigned long recno)`

Iterate **live** records in **slot index order**, skipping tombstones; return `0` when exhausted.
Use these instead of looping `1..count()` (slots can be sparse after deletes). On ring tables,
prefer `fifoFirstRec()` / `fifoNextRec()` for chronological order.

```cpp
for (unsigned long r = db.firstRec(); r != 0; r = db.nextRec(r)) {
    db.readRec(r, EDB_REC rec);
}
```

## `bool isLive(unsigned long recno)`

Returns `true` if `recno` is an allocated, non-tombstoned slot.

## `EDB_Status compact()`

Reconciles `count()` and rebuilds the free list from tombstones (reclaims slots leaked by a crash).
Does not move live records. Returns `EDB_ERROR` on ring tables (use `clear()` instead).

## `EDB_Status enableRingMode()`

Enables fixed-capacity **ring FIFO** mode: append-only, overwrites the oldest record when full.
`deleteRec` and `compact()` return `EDB_ERROR`. Reset the log with `clear()` (examples often wrap
this in a local `deleteAll()` helper).

## `bool ringModeEnabled() const`

Returns whether ring FIFO mode is active.

## `unsigned long fifoFirstRec()` / `unsigned long fifoNextRec(unsigned long recno)`

Iterate live records in **FIFO order** (oldest first). Before the ring has wrapped, order matches
`firstRec()` / `nextRec()`. After wrap, starts at the oldest slot (the next slot to be overwritten).

## `EDB_Status enableStableIds()`

Enables monotonic `record_id` in payload bytes `[0..3]` on each `appendRec` (requires `rec_size >= 4`).
Your struct should reserve those bytes (e.g. `struct { uint32_t id; ... }`). Existing live records
with non-zero ids in that field are respected when enabling.

## `bool stableIdsEnabled() const`

Returns whether stable record ids are active for this table.

## `EDB_Status recordId(unsigned long recno, uint32_t* out_id) const`

Returns the stable `record_id` for a live slot when stable ids are enabled.

## `EDB_Status findRecById(uint32_t record_id, unsigned long* out_recno) const`

Linear scan for a live record by stable id; sets `out_recno` to the current slot address.

## `unsigned int recSize() const`

Returns the stored record size (bytes) of the open table.

## `EDB_Status clear()`

Re-reads the header and recreates the table with the same `table_size` and `rec_size`, resetting
`count()` to 0 and clearing ring head / `next_record_id` when those modes are enabled. Writes a fresh
v3 header.

This is a **destructive logical wipe**: it does not securely erase old record bytes from EEPROM/flash/SD — stale data may remain until overwritten. It is not a substitute for migrating a legacy file to v3 when you need to preserve records (use [MIGRATION.md](MIGRATION.md) instead).

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

- **`recno`** is a 1-based **slot address** for I/O and iteration — not a durable logical record id.
  Put `uint32_t id` (or `enableStableIds()`) in your struct for anything you must find again later.
- Valid `recno` for read/update/delete: allocated slots `1` through `n_slots` that are live (see
  `isLive()`). After deletes, do not assume `1..count()` covers all live rows — use `firstRec` /
  `nextRec`.
- `recno == 0` always returns `EDB_OUT_OF_RANGE`.

## Build flags

Optional compile-time flags (define before `#include` or via `-D` in build properties). See [ENCRYPTION.md](ENCRYPTION.md).

| Flag | Default | Purpose |
|------|---------|---------|
| `EDB_VERIFY_ON_READ` | `1` | Verify each record's CRC on read; `0` = write-only integrity (min read CPU) |
| `EDB_HEADER_REDUNDANT` | `1` | Store the header twice for atomic updates; `0` = single header (discouraged) |
| `EDB_ENABLE_CRYPTO` | off | Include `EDB_Crypto.h` (ChaCha20-Poly1305 record encryption) |
| `EDB_CRYPTO_DEVICE_AUTONOMOUS` | off | On-device wrapped-key extension helpers |
| `EDB_NO_GLOBAL` | off | Omit legacy `extern EDB edb` |
| `EDB_TEST` | off | Test hooks (native tests only) |

Bridge sketch flags (`examples/EDB_SerialBridge/config.h`): `EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO`, `EDB_BRIDGE_MAX_LINE`, `EDB_BRIDGE_ENABLE_BASE64`.

## Gateway REST mapping

The [EDB Gateway](GATEWAY.md) exposes the same semantics over HTTP. Encrypted tables use `stored_rec_size = plaintext_rec_size + 16` at `create()` time.
