# Migrating EDB v1 to v2

EDB 2.0.0 introduces a packed 12-byte v2 header that is identical on all platforms. Legacy v1 files should be migrated before cross-platform use or before writing on ESP32-style 16-byte v1 layouts.

## When to migrate

| Situation | Action |
|-----------|--------|
| New project | Use EDB 2.0.0 directly; `create()` writes v2 |
| SD/SPIFFS file from older EDB | Run `edb_migrate.py` on a PC |
| AVR EEPROM v1 (12-byte header) | Can open and write in place (auto-upgrades header) |
| ESP32 v1 (16-byte header) | Migrate before any writes |
| Move DB between MCU families | Migrate to v2 |

On-device EEPROM migration is not automated. Export data over serial, run `clear()`, and re-import, or recreate the table.

## Using edb_migrate.py

```bash
python tools/edb_migrate.py old.db new.db --arch auto
python tools/edb_migrate.py old.db new.db --arch avr
python tools/edb_migrate.py old.db new.db --arch esp32 --force
```

`--arch auto` tries known v1 layouts and picks the first that passes validation.

## Standalone Windows executable

```bash
pip install pyinstaller
pyinstaller --onefile tools/edb_migrate.py
```

Run `dist/edb_migrate.exe input.db output.db --arch auto`.

## Library behavior after migration

1. Copy the migrated file to your SD/SPIFFS device.
2. Call `db.open(0)` and verify `EDB_OK`.
3. Use normal CRUD operations.

## Verifying migration

```bash
python -m unittest discover -s tools -p "test_*.py"
make -C test test
```

Both test suites include migration round-trip checks.
