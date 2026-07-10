# EDB Tools

Host-side utilities for EDB database files. All tools run on a computer; copy the output file back
to device storage when finished.

## edb_migrate.py

Converts legacy EDB v1/v2 database files to the v3 format.

```bash
python tools/edb_migrate.py input.db output.db --arch auto
python tools/edb_migrate.py input.db output.db --force
```

See script `--help` for `--arch avr|esp32|auto`.

## edb_check.py

Validates v3 headers, slot CRCs, live/tombstone counts, and duplicate stable `record_id` values.

```bash
python tools/edb_check.py mytable.db
python tools/edb_check.py mytable.db --offset 0x1000 --strict
```

Exit code `0` = OK, `1` = errors (or warnings with `--strict`).

## edb_vacuum.py

Physically repacks live slots (removes tombstone holes). Shrinks `n_slots` when possible.

```bash
python tools/edb_vacuum.py input.db output.db --force
python tools/edb_vacuum.py input.db output.db --remap-json remap.json --check
```

**Reindexing:** vacuum may change physical **recno** (slot index). Stable **`record_id`** values in
payload `[0..3]` (when the table uses `enableStableIds()`) are preserved. Apply `--remap-json` if
clients still store recno, or migrate them to `record_id` / `findRecById()`.

On-device: `compact()` repairs the free list without moving records. Vacuum is host-only
([`edb_vacuum.py`](edb_vacuum.py)); serial bridge supports `compact` only. **Ring tables** should
not be vacuumed on the host (they have no tombstones); `edb_grow.py` is safe on ring tables.

## edb_grow.py

Increases the reserved `table_size` of a v3 table so `limit()` is higher. Run on a PC, then copy
the output file back to EEPROM/SD and **update firmware constants** (`table_size`, `storage[]` size,
and any downstream `head_ptr` values if the file has multiple tables).

```bash
python tools/edb_grow.py sensor.db sensor-big.db --add-slots 20 --force
python tools/edb_grow.py sensor.db sensor-big.db --limit 50 --force
python tools/edb_grow.py sensor.db sensor-big.db --table-size 2048 --force
python tools/edb_grow.py sensor.db sensor.db --add-slots 10 --dry-run
```

Growth modes (pick one):

- `--add-slots N` — add N more record slots to current capacity
- `--limit N` — set maximum slots to N
- `--table-size BYTES` — set absolute `table_size` (decimal or `0x` hex)

Existing records and slot addresses are preserved. New space is zero-filled. If another table
starts immediately after this one in the file, that trailing data is shifted and the tool prints
the new `head_ptr` for it.

## Tests

```bash
python -m unittest discover -s tools -p "test_*.py"
```

## Standalone executable (migrate)

```bash
pip install pyinstaller
pyinstaller --onefile tools/edb_migrate.py
```
