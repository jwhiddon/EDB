# EDB Upgrade Guide

This guide describes safe upgrade paths between EDB releases. Read it before replacing library files in a project that already has live data on EEPROM, SD, or SPIFFS.

## Which release should I use?

| Your situation | Recommended release |
|----------------|---------------------|
| Existing project on 1.0.6, want zero risk | **1.0.7** (drop-in) |
| On 1.0.7 and want bug fixes only | Stay on **1.0.7** |
| New project | **2.0.0** |
| Need cross-platform `.db` files (AVR ↔ ESP32) | **2.0.0** + migration tool |
| SD/SPIFFS database created on a different MCU | **2.0.0** + `edb_migrate.py` |

```mermaid
flowchart TD
  v106[1.0.6]
  v107[1.0.7 drop-in]
  v200[2.0.0]
  v106 -->|"Copy EDB.h + EDB.cpp"| v107
  v107 -->|"Replace library"| v200
  v106 -->|"Replace library + migrate if needed"| v200
  v200 -->|"create() on new projects"| v200
```

---

## Path 1: 1.0.6 → 1.0.7 (drop-in)

**Best for:** Any existing 1.0.6 deployment where you want safety fixes without touching stored data.

### Steps

1. Back up your current `EDB.h` and `EDB.cpp` (optional but recommended).
2. Copy [`release/1.0.7/EDB.h`](../release/1.0.7/EDB.h) and [`release/1.0.7/EDB.cpp`](../release/1.0.7/EDB.cpp) into your Arduino `libraries/EDB/` folder.
3. Restart the Arduino IDE.
4. Recompile and upload your sketch — **no sketch changes required**.

### What changes

| Area | 1.0.6 | 1.0.7 |
|------|-------|-------|
| On-disk format | Unchanged | Unchanged |
| Public API | Unchanged | Unchanged |
| `recno == 0` | Silent corruption possible | Returns `EDB_OUT_OF_RANGE` |
| `open()` on bad header | Always `EDB_OK` | Returns `EDB_ERROR` |
| `create()` validation | Minimal | Validates params + read-back |
| Shift ops (`insertRec` / `deleteRec`) | No `malloc` check | Returns `EDB_ERROR` on allocation failure |

### Data migration

**None.** Existing EEPROM, SD, and SPIFFS databases are read and written using the same layout as 1.0.6.

### Rollback

Restore your backed-up 1.0.6 `EDB.h` and `EDB.cpp`. Data on storage is unaffected.

---

## Path 2: 1.0.7 → 2.0.0 (recommended intermediate step)

**Best for:** Projects already on 1.0.7 that are ready for the v2 on-disk format and improved cross-platform support.

### Steps

1. **Back up** all stored data (EEPROM dump, copy `.db` files).
2. Replace library files with root [`EDB.h`](../EDB.h) and [`EDB.cpp`](../EDB.cpp) (version 2.0.0).
3. Choose a storage-specific path below (EEPROM vs SD/SPIFFS).
4. Call `open()` and verify it returns `EDB_OK`.
5. Smoke test: `count()`, read first and last record, append one test record.

### By storage backend

#### AVR internal / external EEPROM (same MCU)

- Existing databases usually **open successfully** with `EDB_OK`.
- The first write operation upgrades the header to the packed v2 format in place (12-byte header).
- Record data at the same byte offset is preserved on AVR-style 12-byte v1 layouts.

#### ESP32 EEPROM / SPIFFS / SD (16-byte v1 header)

- **Migrate before any writes** if you need to preserve existing records (`edb_migrate.py` or copy data out first).
- **`clear()` is a destructive wipe**, not a migration: it resets `count()` to 0 and writes a v2 header, but old record bytes may remain on storage until overwritten. It also changes the record base offset from +16 to +12 — use only when you intend to discard data and start fresh.
- Writes to an unmigrated ESP32 v1 database return `EDB_ERROR`.

#### SD card / SPIFFS file (any origin MCU)

1. Copy the `.db` file to your PC.
2. Run:
   ```bash
   python tools/edb_migrate.py device.db migrated.db --arch auto
   ```
3. Copy `migrated.db` back to the device.
4. Open with `db.open(0)` and confirm `EDB_OK`.

See [MIGRATION.md](MIGRATION.md) and [tools/README.md](../tools/README.md).

---

## Path 3: 1.0.6 → 2.0.0 (direct)

**Best for:** New deployments or when you are prepared to migrate all stored databases in one step.

You may skip 1.0.7 if you accept the larger change set in a single upgrade. The storage-specific steps are the same as [Path 2](#path-2-107--200-recommended-intermediate-step).

**Recommended safer route:** 1.0.6 → 1.0.7 first (verify in production), then 1.0.7 → 2.0.0.

---

## Compatibility matrix

| From | To | Library action | Sketch changes | DB migration | Data risk |
|------|-----|----------------|----------------|--------------|-----------|
| 1.0.6 | 1.0.7 | Copy `release/1.0.7/EDB.*` | None | None | **None** |
| 1.0.7 | 1.0.6 | Restore old files | None | None | None |
| 1.0.7 | 2.0.0 | Copy root `EDB.*` | Optional `open()` check | See backend below | Low–Medium |
| 1.0.6 | 2.0.0 | Copy root `EDB.*` | Optional `open()` check | See backend below | Low–Medium |
| 1.0.x | 2.0.0 | AVR EEPROM, same board | Optional | Auto on first write | **Low** |
| 1.0.x | 2.0.0 | ESP32 v1 EEPROM | Optional | Required before write | **High** without migration |
| 1.0.x | 2.0.0 | SD / SPIFFS file | Optional | `edb_migrate.py` on PC | **Medium** |
| 1.0.x | 2.0.0 | Cross-platform SD file | Optional | `edb_migrate.py --arch auto` | **Required** |
| — | 2.0.0 | New project | Use 2.0.0 API | `create()` writes v2 | None |

### Storage backend summary

| Backend | 1.0.6 → 1.0.7 | 1.0.x → 2.0.0 |
|---------|---------------|---------------|
| AVR EEPROM | No migration | Usually auto-upgrade on write |
| ESP32 EEPROM | No migration | Migrate or recreate before write |
| SD card file | No migration | Run `edb_migrate.py` |
| SPIFFS file | No migration | Run `edb_migrate.py` |
| AT24C1024 / I2C EEPROM | No migration | Same rules as host MCU |

---

## API changes in 2.0.0

| Item | 1.0.6 / 1.0.7 | 2.0.0 | Breaking? |
|------|---------------|-------|-----------|
| `clear()` return type | `void` | `EDB_Status` | No — safe if return value ignored |
| `appendRec()` parameter | `EDB_Rec` | `const EDB_Rec` | No — source-compatible |
| `extern EDB edb` | Declared in header | Still declared (use `#define EDB_NO_GLOBAL` to hide) | No for single-table sketches |
| New `create()` databases | Compiler-dependent v1 layout | Packed 12-byte v2 header | Yes — for cross-platform new DBs |
| `open()` on corrupt DB | 1.0.6: bogus `EDB_OK`; 1.0.7+: `EDB_ERROR` | `EDB_ERROR` | Behaviour fix |

---

## Upgrade checklist

Use this checklist for any upgrade that touches live data:

- [ ] Back up storage (EEPROM dump, copy `.db` file, or export records over serial)
- [ ] Note current library version (`library.properties` → `version=`)
- [ ] Copy the correct `EDB.h` and `EDB.cpp` for your target release
- [ ] If upgrading to 2.0.0 with SD/SPIFFS data, run `edb_migrate.py`
- [ ] Recompile and upload the sketch
- [ ] Call `open()` — expect `EDB_OK` on a healthy database
- [ ] Read `count()` and spot-check first and last records
- [ ] Append one test record and read it back
- [ ] Remove the test record or restore from backup if this is production data

---

## Rollback

| Rollback | Action | Data impact |
|----------|--------|-------------|
| 1.0.7 → 1.0.6 | Restore backed-up 1.0.6 library files | None |
| 2.0.0 → 1.0.7 | Restore 1.0.7 library files | v2 databases may not open on 1.0.7 — restore storage from backup |
| 2.0.0 → 1.0.6 | Restore 1.0.6 library files | Same as above |

Always keep a storage backup taken **before** upgrading to 2.0.0.

---

## Related documentation

- [MIGRATION.md](MIGRATION.md) — v1 to v2 file format and migration tool details
- [FORMAT.md](FORMAT.md) — on-disk header layouts
- [TESTING.md](TESTING.md) — running integrity tests locally
- [release/1.0.7/README.md](../release/1.0.7/README.md) — drop-in release notes
