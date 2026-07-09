# EDB Architecture

EDB (Extended Database Library) is a small sequential record store for Arduino and compatible platforms. It maps a fixed-size table in byte-addressable storage into numbered records.

## Core model

- Records are fixed-size **slots** stored after a redundant, checksummed header.
- Record numbers are **1-based** and **stable**: `recno` never renumbers, even after deletes.
- Deleting a record tombstones its slot; the slot id is reused later via a free list.
- Each slot carries a CRC so a torn write is detected (`EDB_CORRUPT`) rather than trusted.
- Storage backends are pluggable through read/write handlers.

## Storage backends

EDB does not talk to hardware directly. You provide handlers:

| Handler type | Signature | Use when |
|--------------|-----------|----------|
| Byte write | `void(unsigned long addr, uint8_t value)` | EEPROM, simple backends |
| Byte read | `uint8_t(unsigned long addr)` | EEPROM, simple backends |
| Buffer write | `void(unsigned long addr, const byte*, unsigned int len)` | SD, SPIFFS, faster I/O |
| Buffer read | `void(unsigned long addr, byte*, unsigned int len)` | SD, SPIFFS, faster I/O |

Construct `EDB` with either the byte pair or the buffer pair.

## Operation performance

| Operation | Complexity | Notes |
|-----------|------------|-------|
| `appendRec` | O(1) | Reuses a freed slot or grows |
| `readRec` | O(1) | Verifies the slot CRC |
| `updateRec` | O(1) | Rewrites the slot; no header write |
| `deleteRec` | O(1) | Tombstones the slot (no shifting) |
| `insertRec` | O(1) | Allocates a free slot (position not preserved — see below) |
| `firstRec` / `nextRec` | O(slots) total | Iterate live records, skipping tombstones |

Stable slot ids mean `deleteRec`/`insertRec` no longer shift record tails, so they are O(1) and do
not thrash EEPROM/flash. Because ids are stable, after deletes the live records may be **sparse**:
iterate them with `firstRec()`/`nextRec()` rather than looping `1..count()`. `insertRec` allocates
any free slot and does **not** preserve positional order (a deliberate 3.0 change); use `appendRec`.

## Crash safety

- The header is written to two CRC-32-checksummed copies (ping-pong by sequence number), so a power
  loss during a header update always leaves one complete, valid copy.
- Per-slot CRC-16 detects torn record writes.
- Slot bytes are written before the header that publishes them, so an interrupted mutation is benign.

## Header versions

- **v3 (2.0.0+):** redundant 48-byte×2 checksummed header, framed slots. All new databases use v3.
- **v1 / v2 (legacy):** detected on `open()` and reported `EDB_NEEDS_MIGRATION`. Convert offline with
  `tools/edb_migrate.py`; the library never upgrades a file in place.

See [FORMAT.md](FORMAT.md) and [MIGRATION.md](MIGRATION.md).

## Typical workflow

1. Define a record struct.
2. Implement storage handlers.
3. `create(head_ptr, table_size, sizeof(record))` on first use.
4. `open(head_ptr)` on subsequent runs after validating `EDB_OK`.
5. Use `appendRec` for normal inserts; avoid `insertRec(1, ...)` on large tables.

## Gateway and manager (host-side)

```
Browser (Web Crypto) → FastAPI Gateway → Serial transport → EDB_SerialBridge → EDB → storage
```

- **Gateway** (`services/edb-gateway/`): REST API, connection manager, ciphertext-only relay.
- **Manager** (`/manager`): Connect, unlock passphrase locally, CRUD on encrypted payloads.
- **Backend abstraction**: `DeviceBackend` today; `FileBackend` (Phase 2) will use `tools/edb_migrate.py`.

The gateway never holds decryption keys. See [GATEWAY.md](GATEWAY.md) and [ENCRYPTION.md](ENCRYPTION.md).

## Optional encryption (`EDB_Crypto.h`)

Opt-in via `EDB_ENABLE_CRYPTO`. Per-record AEAD at rest; `e2e_blind` (opaque bytes on MCU) or `device_autonomous` (ESP32+). Core `EDB.h` API unchanged.
