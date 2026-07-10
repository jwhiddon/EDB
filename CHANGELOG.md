# Changelog

All notable changes to this project are documented here.

## 2.0.0

> **Pre-release (alpha).** 2.0.0 is being staged as a pre-release (`v2.0.0-alpha.1`) while the v3
> format and the gateway settle. The on-disk format and APIs may still change before the final
> 2.0.0. Not yet recommended for production; pin a specific alpha tag if you depend on it.
>
> **Known limitations (alpha).** Crash-safety is designed-in — slot bytes are written before the
> header that publishes them, the status byte last, behind a dual-CRC32 header — but is not yet
> exercised by fault-injection ("torn write") tests. At-rest record encryption is not yet
> cross-validated between the C++ and Python implementations (the shared vector file is not loaded by
> a test; the gateway is a blind ciphertext relay holding no keys); the serial transport session
> *is* cross-validated. These are tracked for a later alpha.

This is a format-breaking release. The on-disk layout is now v3: a redundant, checksummed
"superblock" header plus framed, individually checksummed record slots. Deletes and inserts are
O(1), power loss can no longer silently corrupt a table, and torn writes and bit-rot are detected
rather than returned as valid data. Legacy v1/v2 files are not read in place — convert them offline
(see [Migration](#migration)).

### Why this release exists

An audit of the previous format and code found three structural defects on the storage layer, plus
several security holes in the encryption, gateway, and serial bridge. The old design could not be
patched around them — the fixes required owning the on-disk format, which is why v3 is a clean break:

1. **A single, unchecksummed header rewritten in place.** Every `appendRec`/`deleteRec`/`insertRec`
   overwrote the one 12-byte header to update the record count. A power loss during that ~12-byte
   write left the table's count and the actual data disagreeing, with no checksum to even detect it —
   the whole table could be silently lost.
2. **No integrity metadata on records or header.** A torn write (half a record persisted) or a
   flipped bit from bit-rot was returned to the caller as valid data. There was no way to tell good
   bytes from bad.
3. **O(n) shifting on delete/insert.** Deleting record 1 of N rewrote the remaining N−1 records to
   close the gap. That was slow, wore out EEPROM/flash cells, and was non-atomic: a power loss
   partway through the shift silently duplicated or dropped records.

v3 addresses each directly; the sections below note which defect each change closes.

### On-disk format (v3)

- **Redundant, atomic header** — *fixes defect 1.* Two 48-byte CRC-32 header copies are written
  ping-pong by a monotonic `seq`; a publish always targets the *stale* copy, never the currently
  valid one. `open()` picks the highest-`seq` copy that passes CRC and self-heals the other. A torn
  header write can now damage at most the stale copy, so a valid header always survives a power loss,
  and a corrupt header is detected instead of trusted. `EDB_HEADER_REDUNDANT=0` collapses to a single
  header on space-starved targets (discouraged — it forfeits this guarantee).
- **Framed, checksummed record slots** — *fixes defect 2.* Each slot is
  `[status:1][payload:rec_size][crc16:2]`. The CRC-16-CCITT is written on every write and verified on
  read (`EDB_VERIFY_ON_READ`); a torn write or bit-flip now returns `EDB_CORRUPT` for that one record
  instead of handing back bad bytes, and never affects the rest of the table. The `status` byte
  distinguishes never-used (`0x00`) from live and tombstone, so an erased or blank region can never
  be mistaken for live data.
- **O(1) tombstone delete + intrusive free-list** — *fixes defect 3.* `deleteRec` marks a slot as a
  tombstone and pushes it onto a free-list threaded through the tombstoned slots themselves (no extra
  storage); `appendRec`/`insertRec` pop from that list or grow the high-water mark. Nothing shifts,
  so a delete is a constant number of writes regardless of table size — deleting the oldest of
  100,000 records dropped from ~2.2M byte-writes to ~53 — and it is atomic, since the header that
  publishes the change is written last.
- **Explicit `data_offset` + 14 reserved header bytes** — so this is the *last* forced format break
  for small additions. Future fields drop into `reserved` with zero rewrite of existing files;
  larger ones bump `data_offset` for new files without moving records.

### Performance

The crash-safety machinery above republishes the 48-byte header on every append, which by itself
made `appendRec` more expensive than the old in-place 12-byte write (73 vs ~33 byte-writes). These
two optimizations recover that cost, so the added integrity is close to free.

- **`EDB_WRITE_IF_DIFFERENT` (default on, byte handlers only).** Most of the 48-byte header
  republish is unchanged bytes (magic, version, sizes, reserved). Reading each byte first and
  skipping the write when it already matches (EEPROM.update semantics) cuts `appendRec` from 73 back
  to 32 raw writes — parity with 1.0.7 — while keeping the dual CRC-32 header and per-record CRC-16.
  A read is ~3000× cheaper than a write on EEPROM/flash, so this is an overwhelming win there; on
  RAM/FRAM handlers where reads and writes cost the same it is a net loss, so set
  `EDB_WRITE_IF_DIFFERENT=0`. Never applied to buffer handlers (SD/SPIFFS), where a block write is a
  single operation.
- **Batch append (`beginBatch()` / `endBatch()`).** For bulk loads, deferring the header publish
  makes each `appendRec` cost only the slot write (~25 bytes) — cheaper than 1.0.7's 33 while still
  writing full integrity metadata. This is safe without giving up crash-safety: `beginBatch()` sets a
  dirty bit (`EDB_HDR_BATCH`) so a crash mid-batch is detected on the next `open()`, which rebuilds
  `n_live`/`n_slots`/`free_head` (and `next_record_id`) from a single scan of the slot region — the
  slots are the ground truth, the header counts are only a cache — and clears the bit. No records are
  lost. `deleteRec`, `clear`, and `compact` are refused mid-batch; ring tables cannot be batched.
- **Header-free updates.** `updateRec` no longer touches the header (the live count is unchanged),
  and with write-if-different a same-sensor update rewrites only the fields that actually changed
  (~7–9 effective bytes vs. a full record) — so v3 updates are faster than the old format's, not just
  safer.
- Benchmark harness and methodology in [docs/BENCHMARK.md](docs/BENCHMARK.md), reproducible via
  `test/bench/run.sh` against a shipped, hash-verified 10,000-record dataset.

### Gateway and encryption

The previous encryption and gateway shipped with exploitable weaknesses; the notes below say what
each change fixes.

- **Real per-record AEAD (`EDB_Crypto.h`)** — the old scheme used one MAC key for every record with
  no binding to a record's identity, so an attacker could swap or replay ciphertexts between records
  and they would still verify; it reused the nonce on in-place updates (a two-time-pad keystream
  reuse); its "Poly1305" was actually a truncated secret-prefix SHA-256; and it compared tags with
  variable-time `memcmp` (a timing oracle). v3 replaces it with RFC-8439 ChaCha20-Poly1305: a fresh
  random per-record nonce carried in the payload (so no two writes ever share keystream), AAD binding
  the record's logical identity (`table_id` and `record_id`) so a ciphertext can't be swapped to a
  different record and still verify, a real Poly1305 tag, and constant-time comparison. Reordering
  ciphertexts *within* a table is authenticated only when you use stable record ids — see
  [docs/ENCRYPTION.md](docs/ENCRYPTION.md).
- **Auth on every gateway route** — routes previously had no authentication at all. Now every route
  requires an API key compared with `hmac.compare_digest`.
- **CORS allowlist + Host-header check** — with no CORS policy, any web page could drive the gateway,
  and a DNS-rebind could reach a loopback bind. Added an explicit origin allowlist (no `*`) and a
  Host-header check.
- **Request size validation** — `rec_size`/`table_size`/`head_ptr` were passed through unvalidated,
  feeding the bridge's fixed-buffer overflow below. They are now bounded before reaching the device.
- **Honest transport encryption** — the old `encrypt:true` transport was a passthrough that also sent
  the pairing token in cleartext. Replaced with a real PSK session (challenge/response pairing, no
  cleartext token), mirrored in the bridge.
- **Host-side `FileBackend`** — reads and writes v3 `.db` files directly, so the gateway and its
  tests run without a physically attached device.
- Browser end-to-end "blind" mode in the Manager keeps keys off both the device and the gateway.

### Serial bridge (`examples/EDB_SerialBridge`)

- **Bounded record sizes** — an attacker-controlled `rec_size` was written into a fixed 128-byte
  stack buffer, a remotely triggerable stack overflow. `create`/`open` now reject any `rec_size`
  that exceeds the buffer, and all I/O is sized by the table's actual record size.
- **Enforced session gate** — the pairing token was set but never checked, so the "paired" state was
  cosmetic. Mutating commands now require a valid session (constant-time token compare) when
  transport crypto is enabled.
- **Scoped JSON parsing** — unscoped `strstr` let a `payload_b64` value containing `"recno":N` hijack
  the command's target record. Key matching is now scoped and a missing/invalid `recno` is rejected
  rather than passed through as `(unsigned long)-1`.
- **Durability** — `flush()` after the publishing header write so a power loss can't leave a
  published count with unwritten slot bytes.

### Host tools

- `edb_migrate.py` — convert v1/v2 → v3 offline (atomic temp+rename), so the MCU carries no legacy
  read/upgrade code paths.
- `edb_check.py` — validate headers, slot CRCs, stable ids, and ring-table invariants.
- `edb_vacuum.py` — repack tombstone holes host-side, preserving each record's logical `record_id`
  (reclaiming the space O(1) deletes leave behind, off-device where the cost is affordable).
- `edb_grow.py` — increase the reserved `table_size` of an existing file offline.
- `edb_v3_io.py` — shared pure-Python v3 reader/writer used by the tools and gateway.
- `gen_datasets.py` — deterministically generate the shipped, hash-verified test datasets.

### New APIs

- `compact()` — on-device free-list repair that drops tombstones without moving live records.
- `enableStableIds()`, `recordId()`, `findRecById()` — because `recno` is now a slot index that can
  be reused after a delete, these provide a durable logical identity (stored in the first 4 payload
  bytes) for callers that need a permanent handle.
- `enableRingMode()`, `fifoFirstRec()`, `fifoNextRec()` — append-only ring FIFO logs.
- `appendRec(rec, &recno)` overload returning the assigned slot handle.
- `beginBatch()`, `endBatch()`, `batchActive()` — deferred-publish batch append (see Performance).
- New `EDB_Status` codes: `EDB_DELETED` (slot is tombstoned), `EDB_CORRUPT` (CRC verification
  failed), `EDB_NEEDS_MIGRATION` (legacy v1/v2 file) — the format now surfaces conditions the old
  API had no way to report.

### Build flags

- `EDB_VERIFY_ON_READ` (default `1`) — verify each record's CRC on read, or `0` to trust it for
  minimum read CPU (the CRC is still written, so data stays verifiable later).
- `EDB_HEADER_REDUNDANT` (default `1`) — dual header; `0` for a single header (discouraged).
- `EDB_WRITE_IF_DIFFERENT` (default `1`) — read-before-write skip on byte handlers (see Performance).

### Behavior changes

These follow directly from the stable-slot model and are intentional breaking changes:

- `recno` is a slot index for I/O and iteration, not a permanent logical id — it no longer renumbers
  after a delete (a consequence of dropping the O(n) shift). Use stable ids
  (`recordId()`/`findRecById()`) for durable identity.
- `insertRec` allocates a free slot; positional order is not preserved (there is no longer a tail to
  shift a record into).
- Ring tables reject `deleteRec` and `compact()`; wipe with `clear()` only.
- Ring tables never return `EDB_TABLE_FULL` when full — the oldest slot is overwritten.

### Migration

- `open()` on a v1 (AVR/ESP32) or v2 file returns `EDB_NEEDS_MIGRATION` and does **not** upgrade the
  file on-device — keeping legacy-format code off the MCU, where RAM and flash are scarce. Convert
  offline with `tools/edb_migrate.py` and copy the file back. See
  [docs/MIGRATION.md](docs/MIGRATION.md) and [docs/UPGRADE.md](docs/UPGRADE.md).

### Testing

The new format's guarantees are only credible if they are tested, so coverage was expanded to
exercise each one:

- Native (Unity) suites cover header ping-pong/self-heal, torn-write crash safety, per-record CRC
  detection, free-list reuse, stable ids, ring FIFO, and the batch/reconcile path — i.e. the exact
  failure modes the old format couldn't survive.
- Edge/scale suite (`test_edb_edge.cpp`, `tools/test_edb_edge.py`): 4k+ record / 1 MiB tables,
  20k-slot sparse tombstone patterns, ring wrap-around, large payloads, and megabyte grow.
- Shipped, deterministically generated datasets (`test/data/sensorlog.bin`) with FNV-1a manifests;
  `tools/test_datasets.py` guards them against drift, and both the C++ device and the Python v3
  reader assert byte-for-byte survival of realistic records through a v1 → v3 migration (proving the
  two implementations agree on the format).

### Not in this release

- On-device vacuum or grow (host tools only — these need more scratch space and time than an MCU can
  spare, and are safe to defer to the host).
- Gateway REST `POST .../compact` (the serial bridge supports a `compact` command).
