# EDB Benchmark

Reproduce with:

```bash
./test/bench/run.sh            # shipped test/data/sensorlog.bin (10,000 records)
./test/bench/run.sh 100000     # regenerate 100k in a temp dir, run against it
```

The dataset is **not generated in the benchmark** — it is a shipped, verifiable file. A 22-byte
record (`id`, timestamp, temperature, humidity, status flags, channel, 8-char tag with per-record
varying fields) is produced deterministically by [`tools/gen_datasets.py`](../tools/gen_datasets.py)
into `test/data/sensorlog.bin` (+ `sensorlog_updates.bin` and a `sensorlog.json` manifest with the
schema and FNV-1a hashes). Every version under test loads the same file, so they write byte-for-byte
identical records; the run prints the FNV-1a hash, which matches the manifest (and
`tools/test_datasets.py` guards the shipped files against drift). `./run.sh <count>` regenerates a
different size in a temp dir without touching the shipped files.

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

## Results (N=10,000, K=16, rec_size=22, byte handlers)

`eff_w` = writes that change a cell (what an `EEPROM.update()` handler physically commits). Per
operation:

| operation | 1.0.7 raw / eff | 2.0.0 WID-off raw / eff | 2.0.0 WID-on (default) |
|-----------|----------------:|------------------------:|-----------------------:|
| `appendRec` | 33 / 23 | 73 / 32 | **32 / 32** |
| `appendRec` inside a batch | — | 25 / 25 | **25 / 25** |
| `updateRec` (new sensor reading) | 22 / 6.8 | 25 / 8.8 | **8.8 / 8.8** |
| `deleteRec` (oldest) | 219,824 / 138,233 | 53 / 10.5 | **10.5 / 10.5** |
| `deleteRec` (newest) | 11 / 1 | 53 / 10.1 | 10.1 |
| `insertRec` | 220,198 / 138,480 | 73 / 32.2 | **32.2 / 32.2** |
| `readRec` | 22 reads | 25 reads | 25 reads |

Whole-workload totals (append N + read N + update N + 3×K mutations + scan):

| | raw writes | effective writes | naive EEPROM time* |
|--|-----------:|-----------------:|-------------------:|
| 1.0.7 | 7,590,539 | 4,725,523 | ~15,594 s |
| 2.0.0 (WID off) | 1,233,056 | 658,782 | ~2,174 s |
| 2.0.0 (WID on) | 658,782 | 658,782 | ~2,174 s |

<sub>*effective writes × 3.3 ms. The whole-workload win is dominated by the O(1) deletes; on a
pure append+read workload v3 costs slightly more (see per-op table).</sub>

Takeaways:
- **`appendRec` reaches raw parity with 1.0.7** (32 vs 33) with write-if-different on, and a
  **batched append (25) is cheaper than 1.0.7** — while keeping the dual CRC32 header *and* the
  per-record CRC16. v3's ~9 extra *effective* append bytes (32 vs 23) buy crash-safety + integrity.
- Realistic `updateRec` (same sensor, new reading) changes only a few fields: **6.8–8.8 effective
  bytes**, and write-if-different removes the wasted raw writes (25 → 8.8).

### 1.0.6 → 1.0.7 is not a performance change

Byte-for-byte identical storage I/O in every phase; 1.0.7 only adds an 11-byte read-back verify in
`create()`. 1.0.7 was a correctness/safety release (it fixed, among other things, `create()` falling
off the end without returning a status in 1.0.6).

### O(n) → O(1) delete/insert

Shift-based versions rewrite the whole tail on every delete or insert. Writes to remove the oldest
record, as the table grows (rec_size 22):

| N | 1.0.7 | 2.0.0 |
|---|------:|------:|
| 10,000 | 219,824 | 53 |
| 100,000 | 2,199,824 | 53 |

v3 is flat regardless of table size; the shift-based cost is strictly linear in N. Every other
per-operation number is identical between N=10,000 and N=100,000.

### The cost of crash safety

v3's `appendRec` republishes a 48-byte CRC32 header (dual copy, ping-pong) on every append. That is
where the header cost comes from — not the per-record CRC16, which is only **2 bytes per record**.

## The two optimisations

### `EDB_WRITE_IF_DIFFERENT` (default **on**, byte handlers only)

Most of the 48-byte header republish is *unchanged* bytes (magic, version, `rec_size`, `table_size`,
reserved…). Only ~7 bytes actually change: `seq`, the low bytes of `n_slots`/`n_live`, and the
4-byte CRC32. Reading a byte before writing it skips the rest.

- `appendRec`: **73 → 32** raw writes (2.3×), reaching 1.0.7 parity. `create`: 96 → 88 (the 8 bytes
  of `free_head = 0xFFFFFFFF` already match erased storage — a nice sanity check that WID works).
- Cost: one read per byte written (`append` reads rise 480k → 1.21M for N=10,000).
- **This is a win only when writes are far more expensive than reads.** On EEPROM/flash (3000:1)
  it is overwhelming. On RAM- or FRAM-backed byte handlers, where reads and writes cost the same,
  it is a net loss — set `-DEDB_WRITE_IF_DIFFERENT=0`.
- Never applied to **buffer handlers** (SD/SPIFFS), where a 48-byte block write is one operation.

### Batch append (`beginBatch()` / `endBatch()`)

The header's `n_live`/`n_slots`/`free_head` are a cache; the per-slot status byte is ground truth.
A batch defers the header publish so each append costs only the slot write:

```cpp
db.beginBatch();
for (...) db.appendRec(rec);   // slot only: 25 writes (22 payload + 2 CRC16 + 1 status)
db.endBatch();                 // one header publish
```

**Cheaper than 1.0.7's 33 writes/append while keeping the dual CRC32 header and per-record CRC16.**

Durability: a crash mid-batch can never corrupt the table — unpublished appends leave LIVE slots the
header does not yet count. `beginBatch()` sets a dirty bit (`EDB_HDR_BATCH`) in the header; the next
`open()` sees it, rebuilds `n_live`/`n_slots`/`free_head` (and `next_record_id`) with one scan of the
slot region, and clears the bit. No records are lost. `deleteRec`, `clear`, and `compact` are refused
while a batch is open, and ring tables cannot be batched.

## Should the record checksum be an option?

**No — not as a performance knob.** The CRC16 is 2 bytes per record, and its CPU cost is noise: a
bitwise CRC16 over a record is a few hundred cycles ≈ tens of µs on a 16 MHz AVR, versus 3.3 ms for
a *single* EEPROM byte write. Disabling it would forfeit torn-write and bit-rot detection — the
library's central integrity guarantee — for a couple of bytes per record, when the two optimisations
above give far more at no safety cost.

The safe knob already exists: `EDB_VERIFY_ON_READ=0` skips CRC *checking* on read (saving CPU and
2 reads per record) while still *writing* the CRC, so data remains verifiable later.

## Caveats

- Host measurement with a RAM-backed handler; it counts I/O operations, not device wall-clock.
- `eff_w` depends on how much your data actually changes. Overwriting a record with mostly-identical
  bytes writes fewer cells than one with random bytes.
- Buffer handlers (SD/SPIFFS) batch I/O at the block level; byte counts translate differently there.
