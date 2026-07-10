# Changelog

All notable changes to this project are documented here.

## 2.0.0

### On-disk format (v3)

- Redundant 48-byte×2 CRC-32 header with monotonic sequence selection on `open()`
- Framed record slots with per-slot CRC-16; tombstone delete without shifting slots
- Legacy v1/v2 files return `EDB_NEEDS_MIGRATION`; convert offline with `tools/edb_migrate.py`

### Gateway and encryption

- FastAPI gateway (`services/edb-gateway/`) with REST API and web Manager UI
- Serial bridge for device relay; optional transport and at-rest encryption
- `EDB_Crypto.h` — ChaCha20-Poly1305 per-record AEAD; browser E2E blind mode in Manager

### Host tools

- `edb_migrate.py` — v1/v2 → v3 conversion
- `edb_check.py` — validate headers, slot CRCs, stable ids, ring tables
- `edb_vacuum.py` — repack tombstone holes (host-only; preserves logical `record_id`)
- `edb_grow.py` — increase reserved `table_size` offline

### New APIs

- `compact()` — on-device free-list repair without moving live records
- `enableStableIds()`, `recordId()`, `findRecById()` — durable logical record identity
- `enableRingMode()`, `fifoFirstRec()`, `fifoNextRec()` — append-only ring FIFO logs
- `appendRec(rec, &recno)` overload for slot handle after insert

### Behavior changes

- `recno` is a slot index for I/O and iteration, not a permanent logical id
- Ring tables reject `deleteRec` and `compact()`; wipe with `clear()` only
- Ring tables never return `EDB_TABLE_FULL` when full — oldest slot is overwritten
- `insertRec` allocates a free slot; positional order is not preserved

### Not in this release

- On-device vacuum or grow
- Gateway REST `POST .../compact` (serial bridge supports `compact` command)
