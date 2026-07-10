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
| 2 | 2 | `flags` | bit0 = encrypted; bit1 = stable record_id; bit2 = ring FIFO mode; bit3 = batch in progress |
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

### Batch in progress (`EDB_HDR_BATCH`)

Set by `beginBatch()` and cleared by `endBatch()`. While set, the header's `n_live`, `n_slots` and
`free_head` are stale — the per-slot `status` byte remains ground truth. If `open()` finds this bit
set (a crash mid-batch), it rebuilds those fields (and `next_record_id`) by scanning the slot region,
then clears the bit. No records are lost. See [BENCHMARK.md](BENCHMARK.md).

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

## Legacy v1/v2 headers and portability

v3 defines every on-disk integer as an explicit fixed-width little-endian type (`uint32_t`,
`uint16_t`). A v3 file is therefore **byte-identical and portable** across 8-bit AVR, 32-bit cores
(ESP32/ESP8266/SAMD/RP2040/STM32), and the host tools — the same bytes open everywhere, and no
`--arch` hint is ever needed to read one.

The legacy **v1** format was *not* portable, because it declared `table_size` as a C `unsigned int`,
whose width is architecture-dependent:

| Format | `table_size` width | Max `table_size` | Header size |
|--------|--------------------|------------------|-------------|
| v1 on 8-bit AVR | **16-bit** | 65,535 B (~64 KB) | 12 B |
| v1 on 32-bit cores | 32-bit | ~4 GB | 16 B |
| v2 | 32-bit | ~4 GB | 12 B |
| v3 | 32-bit | ~4 GB | 48 B ×2 |

- The 16-bit field exists **only on classic 8-bit AVR** — ATmega328P (Uno/Nano/Pro Mini), ATmega2560
  (Mega), ATmega32U4 (Leonardo/Micro) — where `unsigned int` is 2 bytes. On any 32-bit core the
  *same* v1 source compiled `table_size` to 4 bytes (and the header to 16 bytes instead of 12).
- Because the layout depends on the compiler's `int` width, a v1 file written on AVR is not readable
  as-is on ESP32 and vice versa. This is why [`edb_migrate.py`](../tools/edb_migrate.py) needs
  `--arch avr|esp32` (or `--arch auto`) to parse a v1 source — see [MIGRATION.md](MIGRATION.md).
- Consequently a v1 AVR table cannot exceed ~64 KB total: `floor((65535 − 12) / rec_size)` records —
  e.g. **2,978** records at a 22-byte `rec_size`. v2 and v3 use a 32-bit `table_size` on every
  architecture, so this ceiling does not apply to them.

v3 removes the ambiguity by design: fixed-width types mean the header is the same on every MCU and
the 16-bit limit is gone.
