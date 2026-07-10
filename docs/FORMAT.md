# EDB On-Disk Format

All multi-byte integers are **little-endian**. EDB 2.0.0 and later use the **v3** format
described here. Legacy v1/v2 files are detected on `open()` and reported as
`EDB_NEEDS_MIGRATION`; convert them offline with [`tools/edb_migrate.py`](../tools/edb_migrate.py)
(see [MIGRATION.md](MIGRATION.md)). The library never upgrades a file in place.

## v3 header (redundant, checksummed)

The table begins with **two 48-byte header copies** (96 bytes total). Each write publishes to the
*other* copy with an incremented `seq`, so a torn/interrupted header write always leaves one
complete, valid copy. `open()` selects the valid copy with the highest `seq`.

Each 48-byte copy:

| Offset | Size | Field | Notes |
|--------|------|-------|-------|
| 0 | 1 | `magic` | `0xDB` |
| 1 | 1 | `version` | `3` |
| 2 | 2 | `flags` | bit0 = encrypted; bit1 = stable record_id; bit2 = ring FIFO mode |
| 4 | 4 | `seq` | monotonic; higher copy wins on open |
| 8 | 4 | `n_slots` | high-water mark of allocated slots |
| 12 | 4 | `n_live` | live record count (`count()`) |
| 16 | 2 | `rec_size` | stored bytes per record |
| 18 | 2 | `slot_stride` | on-disk bytes per slot |
| 20 | 4 | `table_size` | total bytes reserved for the table |
| 24 | 4 | `free_head` | free-list head slot index, or `0xFFFFFFFF` |
| 28 | 2 | `data_offset` | record region start, relative to `head_ptr` (default 96) |
| 30 | 4 | `next_record_id` | when flags bit1 set: next monotonic id for `enableStableIds()` |
| 34 | 4 | `ring_head` | when flags bit2 set: next slot index to overwrite (LE32) |
| 38 | 6 | `reserved` | zero; room for future fields without a file rewrite |
| 44 | 4 | `header_crc` | CRC-32 over bytes 0..43 |

`header_crc` is the standard reflected CRC-32 (poly `0xEDB88320`, init/xorout `0xFFFFFFFF`).

## Record slots

Records are fixed-size **slots** addressed by a 1-based **`recno`** (slot index + 1). Slots are not
shifted on delete — a deleted slot is tombstoned in place. **`recno` is not a durable record identity:**
use a `record_id` field in your payload (or `enableStableIds()`) for anything that must survive slot
reuse, ring overwrite, or host-side vacuum. Use `firstRec()` / `nextRec()` to walk live records in slot
order; on ring tables use `fifoFirstRec()` / `fifoNextRec()` for FIFO order.

```
slot = [ status : 1 ][ payload : rec_size ][ crc16 : 2 ]
slot_stride = 1 + rec_size + 2
```

- `status`: `0xA5` = LIVE, `0x5A` = TOMBSTONE, anything else = never-used/empty.
- `crc16`: CRC-16/CCITT (poly `0x1021`, init `0xFFFF`, no reflection) over `status || payload`.
  Verified on read (unless `EDB_VERIFY_ON_READ=0`); a mismatch returns `EDB_CORRUPT`.
- Record `recno` N lives at `head_ptr + data_offset + (N-1) * slot_stride`.
- Maximum records: `limit = (table_size - data_offset) / slot_stride`.

### Free list

When `rec_size >= 4`, a TOMBSTONE slot stores the next free slot index (LE32) in the first 4 bytes
of its payload region, forming an intrusive singly-linked free list headed by `free_head`.
`appendRec` pops from this list (O(1)) or grows `n_slots`. For `rec_size < 4` there is no intrusive
list; freed slots are reclaimed by a linear scan when the table is otherwise full.

### `recno` vs record identity

| Use | Mechanism |
|-----|-----------|
| Read / update / delete right now | `recno` from the last `appendRec(&recno)` or iteration |
| Walk live records | `firstRec()` / `nextRec()` (slot order); `fifoFirstRec()` / `fifoNextRec()` on ring tables |
| Bookmarks, foreign keys, crypto AAD | Your own `uint32_t id` field or `enableStableIds()` + `findRecById()` |

**Do not treat `recno` as permanent.** It can become stale when:

- A slot is tombstoned and later **reused** for a different record
- A **ring/FIFO** table overwrites the oldest slot in place (same `recno`, new payload)
- Host **`edb_vacuum.py`** repacks slots (physical `recno` changes)

In normal v3 operation, other slots keep their index while a different slot is deleted — but that
is an addressing convenience, not a promise that `recno` names a fixed logical row forever.

When header flags bit1 (`EDB_HDR_STABLE_IDS`) is set via `enableStableIds()`:

- The first 4 bytes of each live payload hold a monotonic **`record_id`** (assigned on `appendRec`).
- `recordId()` / `findRecById()` look up by that logical id.
- `edb_vacuum.py --remap-json` reports `{old_recno, new_recno, record_id}` if you still hold a `recno`.

Host-only [`tools/edb_vacuum.py`](../tools/edb_vacuum.py) repacks tombstone holes and may change `recno`.

### Ring FIFO mode (`EDB_HDR_RING`)

`enableRingMode()` makes the table **append-only**. When full, `appendRec` **overwrites the oldest**
slot in place (no tombstones). `deleteRec` and `compact()` return `EDB_ERROR`; wipe with `clear()`
(`deleteAll()` in examples is just a sketch helper that calls `clear()`).

Walk chronological order with `fifoFirstRec()` / `fifoNextRec()` once the ring has wrapped; before
that, they match `firstRec()` / `nextRec()`.

### Example

Two 4-byte records (`rec_size = 4`, `slot_stride = 7`, `data_offset = 96`):

```
0    DB 03 00 00 ...            header copy 0 (seq, counts, crc32 at +44)
48   DB 03 00 00 ...            header copy 1
96   A5 01 00 00 00 <crc16>     slot 0 (recno 1) = 1, LIVE
103  A5 02 00 00 00 <crc16>     slot 1 (recno 2) = 2, LIVE
```

## Crash-safety summary

- The header is atomic across power loss (dual CRC-checksummed copies, publish-newest-wins).
- A torn record write is detected via the per-slot CRC (`EDB_CORRUPT`), never returned as valid.
- `append`/`update`/`delete` write slot bytes before publishing the header. A crash in that window
  is benign: the unpublished change is either ignored or leaves `count()` off by at most one / a
  freed slot unreclaimed until `compact()`. Reads always honor the per-slot status byte.

## Encrypted records

The core library is encryption-agnostic: an encrypted record is just an opaque payload whose
`rec_size` already accounts for the crypto envelope. See [ENCRYPTION.md](ENCRYPTION.md).
