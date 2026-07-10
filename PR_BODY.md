## Summary

- **v3 on-disk format** — redundant CRC header, framed slots with per-slot CRC, tombstone delete
- **Gateway + Manager** — FastAPI REST, web UI, serial bridge; ciphertext-only relay when encrypted
- **Encryption** — optional ChaCha20-Poly1305 at rest (`EDB_Crypto`) and browser E2E blind mode
- **Host tools** — `edb_migrate.py`, `edb_check.py`, `edb_vacuum.py`, `edb_grow.py`
- **New APIs** — `compact`, `enableStableIds` / `findRecById`, `enableRingMode` / `fifoFirstRec` / `fifoNextRec`
- **Documentation sweep** — consistent `recno` (slot handle) vs `record_id` (logical id) semantics

## Breaking / behavior changes

- Legacy v1/v2 files must be migrated offline; `open()` returns `EDB_NEEDS_MIGRATION`
- `recno` does not shift on delete but may be reused — use payload `record_id` or `enableStableIds()` for durable identity
- Ring tables: `deleteRec` and `compact()` return `EDB_ERROR`; full ring overwrites oldest slot (no `EDB_TABLE_FULL`)
- `insertRec` no longer preserves positional order

## Test plan

- [ ] `make -C test test` — native 2.0.0 suite (stable ids, ring FIFO, crypto)
- [ ] `make -C test test-107` — 1.0.7 drop-in compatibility
- [ ] `python -m unittest discover -s tools -p "test_*.py"` — migrate, vacuum, grow
- [ ] `python scripts/check_api.py` — public API surface
- [ ] `pytest services/edb-gateway/tests` — gateway REST and file backend
- [ ] Spot-check docs: `recno` vs `record_id`, ring FIFO, encryption AAD identity
- [ ] Arduino compile: `EDB_Simple` (AVR), `EDB_SerialBridge` (ESP32) if hardware CI not run locally
