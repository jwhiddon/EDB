# EDB Tools

Host-side utilities for EDB database files. All tools run on a computer; copy the output file back
to device storage when finished.

## edb_migrate.py

Converts legacy EDB v1/v2 database files to v2 or v3 (choose with `--to`; default `v3`).

```bash
python tools/edb_migrate.py input.db output.db                 # v1/v2 -> v3 (default)
python tools/edb_migrate.py input.db output.db --to v2         # v1 -> v2 (legacy firmware)
python tools/edb_migrate.py input.db output.db --arch avr --force
```

`v3` is the recommended target (redundant CRC32 header, per-record CRC16, O(1) delete). `v1 -> v2`
is a header rewrite only — record bytes are copied verbatim — useful when targeting older firmware
that still reads the packed v2 format. Downgrading `v3 -> v2` is intentionally refused (it would drop
integrity metadata). See `--help` for `--arch avr|esp32|auto` and `--table-size`.

## edb_make_v1.py

Generates a legacy v1 (AVR/ESP32) database file, mainly for testing the converter above. Record
contents come from one of three sources:

- **generated** — seeded pseudo-random bytes, reproducible for a given `--seed` (default);
- **`--from-raw FILE`** — raw record bytes copied from any file;
- **`--from-dataset PATH`** — a [`gen_datasets.py`](gen_datasets.py) output (the `sensorlog.json`
  manifest or its directory). This reads `record_size` and `count` from the manifest, loads the
  matching `.bin`, and verifies it against the manifest's size and FNV-1a hash — so you don't hand-
  specify `--rec-size`/`--count` and a stale/corrupt dataset is caught early.

```bash
python tools/edb_make_v1.py legacy.db --arch avr --rec-size 22 --count 64
python tools/edb_make_v1.py legacy.db --rec-size 22 --count 64 --from-raw test/data/sensorlog.bin
python tools/edb_make_v1.py legacy.db --from-dataset test/data --arch esp32   # reads the manifest
python tools/edb_make_v1.py legacy.db --rec-size 4 --count 8 --table-size 128 --force
```

`--rec-size`/`--count` may still be given alongside `--from-dataset` to take a subset (an explicit
`--rec-size` that disagrees with the manifest is rejected). Note the AVR `table_size` field is 16-bit,
so large datasets need `--arch esp32`. Because generation is deterministic, a test can regenerate the
same records and assert they survive `edb_migrate.py` byte-for-byte. See `--help` for all options.

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
