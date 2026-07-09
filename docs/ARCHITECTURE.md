# EDB Architecture

EDB (Extended Database Library) is a small sequential record store for Arduino and compatible platforms. It maps a fixed-size table in byte-addressable storage into numbered records.

## Core model

- Records are fixed-size structs stored contiguously after a header.
- Record numbers are **1-based** (`recno` starts at 1).
- The header tracks `n_recs`, `rec_size`, and `table_size`.
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
| `appendRec` | O(1) | Preferred way to add records |
| `readRec` | O(1) | |
| `updateRec` | O(1) | |
| `deleteRec` | O(n) | Shifts trailing records down |
| `insertRec` | O(n) | Shifts trailing records up |

When buffer handlers are configured, `deleteRec` and `insertRec` shift record tails in a single block read/write instead of one record at a time.

## Header versions

- **v2 (2.0.0+):** 12-byte packed header, cross-platform compatible. All new databases use v2.
- **v1 (legacy):** Compiler-dependent padding. Opened read-only when detected; AVR-style 12-byte v1 files upgrade in place on write. ESP32-style 16-byte v1 files require migration before writes.

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
