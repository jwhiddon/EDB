# Migrating legacy files to v3

EDB 2.0.0 uses the **v3** on-disk format (redundant checksummed header + framed record slots; see
[FORMAT.md](FORMAT.md)). Legacy **v1** (AVR/ESP32) and **v2** (packed 12-byte header) files are not
read in place — `open()` returns `EDB_NEEDS_MIGRATION`. Convert them **offline** on a computer with
[`tools/edb_migrate.py`](../tools/edb_migrate.py), then copy the result back to the device.

On-device migration is intentionally not supported: rewriting a whole database on an MCU is slow and
risky. Do it on a PC where the write is atomic and verifiable.

## When to migrate

| Situation | Action |
|-----------|--------|
| New project | Use 2.0.0 directly; `create()` writes v3 |
| SD/SPIFFS `.db` from older EDB | Run `edb_migrate.py` on a PC, copy back |
| AVR/ESP32 EEPROM with v1/v2 data | Dump to a file, migrate on a PC, write back |
| `open()` returns `EDB_NEEDS_MIGRATION` | The file is v1/v2 — migrate it |

## Using edb_migrate.py

```bash
python tools/edb_migrate.py old.db new.db            # auto-detect source layout
python tools/edb_migrate.py old.db new.db --arch avr
python tools/edb_migrate.py old.db new.db --arch esp32 --force
python tools/edb_migrate.py old.db new.db --table-size 16384   # override v3 capacity
```

- Accepts v1 (AVR 12-byte, ESP32 16-byte) and v2 (packed 12-byte) sources.
- `--arch auto` tries the known v1 layouts and picks the first that validates.
- **Why `--arch` is needed:** the v1 header layout depends on the compiler's `int` width, so an AVR
  v1 file (12-byte header, 16-bit `table_size`, ~64 KB max) and an ESP32 v1 file (16-byte header,
  32-bit `table_size`) are not interchangeable. `--arch` (or `auto`) tells the tool which to expect.
  v3 uses fixed-width types and is portable across all cores, so nothing needs `--arch` to read it.
  Details in [FORMAT.md](FORMAT.md#legacy-v1v2-headers-and-portability).
- Records migrate to **dense slots with recno 1..N** at migration time. `recno` is a slot index,
  not a durable logical record id — use a field in your struct or `enableStableIds()` for that.
  The original record capacity is preserved unless you override it with `--table-size`. Because v3
  has a larger header and per-slot CRC, the migrated file is larger than the source — make sure the
  destination medium has room (use `--table-size` to fit).
- Writes are **atomic** (temp file + rename), so an interrupted in-place migration
  (`input == output`) cannot corrupt the source.

## Behavior change to be aware of

v3 uses **slot addressing**: `deleteRec` tombstones a slot without shifting others, so the live set
can be sparse. Iterate with `firstRec()`/`nextRec()` instead of looping `1..count()`. `recno` is not
a permanent record name — store a logical id in your payload when you need durable references.
`insertRec` no longer preserves positional order (it allocates a free slot). See
[ARCHITECTURE.md](ARCHITECTURE.md) and [API.md](API.md).

## After migration

1. Copy the migrated file to your SD/SPIFFS device (or write the EEPROM image back).
2. Call `db.open(0)` and verify it returns `EDB_OK`.
3. Spot-check with `count()` and `readRec()` / `firstRec()`.

## Verifying the tool

```bash
python -m unittest discover -s tools -p "test_*.py"   # migration unit tests
make -C test test                                     # native suite opens a migrated fixture
```

The native suite opens a fixture produced by this tool, cross-validating that the Python and C++
implementations agree on the v3 CRC layout.
