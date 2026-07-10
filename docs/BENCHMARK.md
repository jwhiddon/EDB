# EDB Benchmark

Reproduce with:

```bash
./test/bench/run.sh 1024 16      # N records, K mutations
```

## What is measured

On EEPROM/flash the cost of a database operation is dominated by **storage byte I/O**, not CPU:
an AVR internal-EEPROM byte write takes **~3.3 ms**, a read **~1 µs** — a ratio of ~3000:1. So the
benchmark counts handler calls rather than wall-clock:

| column | meaning |
|--------|---------|
| `writes` | calls to the byte write handler |
| `eff_w` | writes whose value actually differs from what is stored (real EEPROM cell writes) |
| `reads` | calls to the byte read handler |

Every record carries distinct bytes; otherwise `EDB_WRITE_IF_DIFFERENT` would legitimately skip
rewriting slots that already hold identical data and the numbers would be meaningless.

The workload performs the same *logical* operations on every version. Note that shift-based
versions renumber on delete while v3 tombstones, so "delete the oldest record K times" is
`deleteRec(1)` on 1.0.x and `deleteRec(firstRec())` on v3.

## Results (N=1024, K=16, rec_size=8, byte handlers)

Writes per operation:

| operation | 1.0.6 / 1.0.7 | 2.0.0 (WID off) | 2.0.0 (WID on, default) |
|-----------|--------------:|----------------:|------------------------:|
| `appendRec` | 19 | 59 | **18** |
| `appendRec` inside a batch | — | 11 | **4** |
| `updateRec` | 8 | 11 | **4** |
| `deleteRec` (oldest) | 8,135 | 53 | **12.5** |
| `deleteRec` (newest) | 11 | 53 | 12.1 |
| `insertRec` | 8,271 | 59 | **18.2** |
| `readRec` | 8 reads | 11 reads | 11 reads |

### 1.0.6 → 1.0.7 is not a performance change

Byte-for-byte identical storage I/O in every phase; 1.0.7 only adds an 11-byte read-back verify in
`create()`. 1.0.7 was a correctness/safety release (it fixed, among other things, `create()` falling
off the end without returning a status in 1.0.6).

### O(n) → O(1) delete/insert

Shift-based versions rewrite the whole tail on every delete or insert. Writes to remove the oldest
record, as the table grows:

| N | 1.0.6 / 1.0.7 | 2.0.0 |
|---|--------------:|------:|
| 64 | 455 | 53 |
| 256 | 1,991 | 53 |
| 1,024 | 8,135 | 53 |
| 4,096 | 32,711 | 53 |

### The cost of crash safety

v3's `appendRec` republishes a 48-byte CRC32 header (dual copy, ping-pong) on every append. That is
where the 59 writes came from — not the per-record CRC16, which is only **2 of 59 writes (3.4%)**.

## The two optimisations

### `EDB_WRITE_IF_DIFFERENT` (default **on**, byte handlers only)

Most of the 48-byte header republish is *unchanged* bytes (magic, version, `rec_size`, `table_size`,
reserved…). Only ~7 bytes actually change: `seq`, the low bytes of `n_slots`/`n_live`, and the
4-byte CRC32. Reading a byte before writing it skips the rest.

- `appendRec`: **59 → 18** writes (3.3×). `create`: 96 → 88 (the 8 bytes of
  `free_head = 0xFFFFFFFF` already match erased storage).
- Cost: one read per byte written (`append` reads rise 49k → 110k for N=1024).
- **This is a win only when writes are far more expensive than reads.** On EEPROM/flash (3000:1)
  it is overwhelming. On RAM- or FRAM-backed byte handlers, where reads and writes cost the same,
  it is a net loss — set `-DEDB_WRITE_IF_DIFFERENT=0`.
- Never applied to **buffer handlers** (SD/SPIFFS), where a 48-byte block write is one operation.

### Batch append (`beginBatch()` / `endBatch()`)

The header's `n_live`/`n_slots`/`free_head` are a cache; the per-slot status byte is ground truth.
A batch defers the header publish so each append costs only the slot write:

```cpp
db.beginBatch();
for (...) db.appendRec(rec);   // slot only: ~11 writes (4 with WID on this data)
db.endBatch();                 // one header publish
```

**Cheaper than 1.0.6's 19 writes/append while keeping the dual CRC32 header and per-record CRC16.**

Durability: a crash mid-batch can never corrupt the table — unpublished appends leave LIVE slots the
header does not yet count. `beginBatch()` sets a dirty bit (`EDB_HDR_BATCH`) in the header; the next
`open()` sees it, rebuilds `n_live`/`n_slots`/`free_head` (and `next_record_id`) with one scan of the
slot region, and clears the bit. No records are lost. `deleteRec`, `clear`, and `compact` are refused
while a batch is open, and ring tables cannot be batched.

## Should the record checksum be an option?

**No — not as a performance knob.** The CRC16 is 2 of 59 raw writes (3.4%), and its CPU cost is
noise: a bitwise CRC16 over 8 bytes is ~500 cycles ≈ 31 µs on a 16 MHz AVR, versus 3.3 ms for a
*single* EEPROM byte write. Disabling it would forfeit torn-write and bit-rot detection — the
library's central integrity guarantee — for a few percent, when the two optimisations above give far
more at no safety cost.

The safe knob already exists: `EDB_VERIFY_ON_READ=0` skips CRC *checking* on read (saving CPU and
2 reads per record) while still *writing* the CRC, so data remains verifiable later.

## Caveats

- Host measurement with a RAM-backed handler; it counts I/O operations, not device wall-clock.
- `eff_w` depends on how much your data actually changes. Overwriting a record with mostly-identical
  bytes writes fewer cells than one with random bytes.
- Buffer handlers (SD/SPIFFS) batch I/O at the block level; byte counts translate differently there.
