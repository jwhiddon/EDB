# EDB Examples

## Getting started

| Sketch | Shows |
|--------|-------|
| [EDB_Simple](EDB_Simple/) | Minimal create / append / read on the internal EEPROM |
| [EDB_MultiTable](EDB_MultiTable/) | Several tables in one storage area (`nextTableOffset`) |

## v3 feature demos (RAM-backed — run on any board)

| Sketch | Shows |
|--------|-------|
| [EDB_Iteration](EDB_Iteration/) | Stable slot IDs, tombstone `deleteRec`, `appendRec(&recno)`, `firstRec`/`nextRec`/`isLive`, `compact()` |
| [EDB_Integrity](EDB_Integrity/) | Per-record CRC (`EDB_CORRUPT` detection) and recovery from a damaged redundant-header copy |
| [EDB_Migration](EDB_Migration/) | Detecting a legacy v1/v2 file (`EDB_NEEDS_MIGRATION`) and the offline migration path |
| [EDB_Encrypted](EDB_Encrypted/) | At-rest ChaCha20-Poly1305 (`edb_crypto_seal_record`/`open_record`) — build with `-DEDB_ENABLE_CRYPTO` |

## Storage backends

| Sketch | Backend |
|--------|---------|
| [EDB_Internal_EEPROM](EDB_Internal_EEPROM/) | AVR/ESP internal EEPROM |
| [EDB_AT24C1024](EDB_AT24C1024/) | AT24C1024 I²C EEPROM |
| [EDB_24XX512](EDB_24XX512/) | 24XX512 I²C EEPROM |
| [EDB_SDCARD](EDB_SDCARD/) · [EDB_SDCARD_Optimized](EDB_SDCARD_Optimized/) | SD card (byte vs buffered handlers) |
| [EDB_SPIFFS](EDB_SPIFFS/) · [EDB_SPIFFS_Optimized](EDB_SPIFFS_Optimized/) | SPIFFS (byte vs buffered handlers) |

## Gateway / host integration

| Sketch | Shows |
|--------|-------|
| [EDB_SerialBridge](EDB_SerialBridge/) | NDJSON serial protocol for the [EDB Gateway](../services/edb-gateway/), with optional PSK transport encryption and on-device at-rest crypto |

## Notes for v3

- Record IDs are **stable**: `deleteRec` tombstones a slot and `recno` never renumbers. Iterate live
  records with `firstRec()`/`nextRec()`, not `for (recno = 1; recno <= count(); ...)`.
- `deleteRec` / `insertRec` are **O(1)**; `insertRec` allocates a free slot (position not preserved).
- Legacy v1/v2 files are not read in place — migrate them offline with
  [`tools/edb_migrate.py`](../tools/edb_migrate.py). See [docs/MIGRATION.md](../docs/MIGRATION.md).
