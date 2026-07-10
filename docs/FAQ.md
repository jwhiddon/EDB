# EDB Frequently Asked Questions

## Can I store `String` or `char*` in a record?

No. Records are fixed-size byte blobs. Use fixed buffers:

```cpp
struct LogEvent {
  int id;
  char name[16];  // OK
};
```

`String` and `char*` store pointer values that are meaningless after a power cycle.

## Can I partially update one field in a record?

`updateRec()` always writes the full record. Read-modify-write instead:

```cpp
db.readRec(recno, EDB_REC rec);
rec.temperature = 72;
db.updateRec(recno, EDB_REC rec);
```

See [examples/EDB_ReadModifyWrite](../examples/EDB_ReadModifyWrite/).

## How do I use multiple tables?

`head_ptr` is a **byte offset** in storage, not a table index. Space tables so they do not overlap:

```cpp
#define TABLE_A_SIZE 512
#define TABLE_B_HEAD (0 + TABLE_A_SIZE)

EDB dbA(&writer, &reader);
EDB dbB(&writer, &reader);
dbA.openOrCreate(0, TABLE_A_SIZE, sizeof(RecordA));
dbB.openOrCreate(TABLE_B_HEAD, TABLE_B_SIZE, sizeof(RecordB));
```

See [API.md](API.md) (Multiple tables) and [examples/EDB_MultiTable](../examples/EDB_MultiTable/).

## Why is `insertRec(1, …)` so slow on SPIFFS/SD?

Insert and delete shift trailing records — O(n) per operation on legacy v1/v2 layouts. In **v3**, `deleteRec` and `insertRec` are O(1) slot operations, but random inserts are still uncommon for logging. Prefer `appendRec()` for logging. See [EDB_AppendOnlyLogger](../examples/EDB_AppendOnlyLogger/).

## Why do ESP8266/ESP32 EEPROM examples return zeros?

Call `EEPROM.begin(size)` before any read/write on ESP platforms. See [EDB_Simple](../examples/EDB_Simple/EDB_Simple.ino).

## Why does SD card storage return wrong or duplicate records?

Arduino `FILE_WRITE` includes `O_APPEND`, which breaks `seek()`. Open with `O_READ | O_WRITE | O_CREAT` on AVR, or `"r+"` / `"w+"` on ESP32. See [EDB_SDCARD](../examples/EDB_SDCARD/EDB_SDCARD.ino).

## Why does `open()` return `EDB_ERROR` on ESP32 v1 databases?

16-byte ESP32 v1 headers must be migrated before writes. Use [edb_migrate.py](../tools/edb_migrate.py) or `clear()` for an intentional wipe (see [UPGRADE.md](UPGRADE.md)).

## Can I use nested structs in records?

Plain POD structs work if `sizeof()` is stable on your target. Avoid pointers, virtual methods, and platform-dependent padding surprises. When in doubt, use fixed-width types (`uint32_t`, etc.).

## Should I store `recno` and look up records later?

No — **`recno` is a slot handle**, not a permanent record name. It goes stale when the slot is
reused after delete, overwritten in a ring/FIFO table, or repacked by host `edb_vacuum.py`.

Put a **`uint32_t id`** (or similar) in your record struct, or call `enableStableIds()` and use
`findRecById()`. Use `fifoFirstRec()` / `fifoNextRec()` on ring tables for oldest-first order.

**Ring tables:** `enableRingMode()` is append-only — `deleteRec` returns `EDB_ERROR`. Wipe with
`clear()` (many examples define a local `deleteAll()` that calls `clear()`).

## How do I change the record schema?

There is no in-place schema migration. Create a new table, migrate records in application code, or use `clear()` and start fresh (data loss). See [examples/EDB_SchemaRotation](../examples/EDB_SchemaRotation/).

## Why does the SPIFFS example trigger watchdog resets?

Byte-mode handlers with `flush()` on every byte block the CPU for long inserts. Use [EDB_SPIFFS_Optimized](../examples/EDB_SPIFFS_Optimized/) and prefer `appendRec`. Add `yield()` in long loops if needed.

## Can I read a `.db` file on my PC?

Copy the file from SD/SPIFFS and use [tools/edb_migrate.py](../tools/edb_migrate.py) to inspect or convert format. For live export, use the [EDB Gateway](GATEWAY.md) or read records over serial in your sketch.

## My table is full (`EDB_TABLE_FULL`). Can I add capacity?

On the device, `table_size` is fixed at `create()` time. Grow the file on a PC with [tools/edb_grow.py](../tools/edb_grow.py), then copy it back and update your sketch (`table_size`, `storage[]` size, and any downstream `head_ptr` if you use multiple tables). Example:

```bash
python tools/edb_grow.py sensor.db sensor-big.db --add-slots 20 --force
```

## What are `e2e_blind` and `device_autonomous` encryption?

- **e2e_blind:** The device stores ciphertext only; the manager encrypts/decrypts in the browser. Strongest E2E; works on small AVRs without crypto libraries.
- **device_autonomous:** ESP32+ firmware encrypts sensor data without the host. Table key is wrapped in the header.

See [ENCRYPTION.md](ENCRYPTION.md).

## Why doesn't the gateway decrypt my data?

By design — the gateway is a dumb relay so a compromised PC cannot read your records from server memory or logs. Enter your passphrase in the manager UI only.

## I lost my encryption passphrase

There is no recovery. Back up wrapped keys when using autonomous mode.

## Will encryption fit on Arduino Uno?

The full gateway bridge + transport crypto does not fit in 2 KB SRAM. Use ESP32 for the bridge, or store pre-encrypted blobs on Uno with core EDB only. See [ENCRYPTION.md](ENCRYPTION.md) RAM tiers.
