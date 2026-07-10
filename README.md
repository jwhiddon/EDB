# EDB 1.0.7 Drop-in Release

Safety fixes for existing EDB 1.0.6 users. Replace `EDB.h` and `EDB.cpp` in your Arduino libraries folder — no sketch changes or database migration required.

## What changed

- Fixed `recno == 0` corruption on delete/update/insert
- `open()` validates header and returns `EDB_ERROR` on corrupt data
- `create()` validates parameters and verifies the write
- `malloc` failure handling in shift operations
- Block-shift optimization when buffer handlers are used
- `limit()` guard against division by zero

## What did NOT change

- On-disk header layout (same as 1.0.6)
- Public API (`void clear()`, `extern EDB edb`, etc.)
- Record numbering and table layout

## Multiple tables (1.0.7)

Use **separate `EDB` instances** (or call `open(head_ptr)` before each table). `head_ptr` is a byte offset, not a table number:

```cpp
#define TABLE_A_SIZE 512
EDB dbEvents(&writer, &reader);
EDB dbConfig(&writer, &reader);
dbEvents.create(0, TABLE_A_SIZE, sizeof(EventRecord));
dbConfig.create(TABLE_A_SIZE, CONFIG_TABLE_SIZE, sizeof(ConfigRecord));
```

2.0.0 adds `nextTableOffset()` and `openOrCreate()` helpers; see [docs/API.md](../../docs/API.md).

See [docs/UPGRADE.md](../../docs/UPGRADE.md) for upgrading to 2.0.0 later.
