# AGENT.md — orientation for AI coding agents

This file explains the EDB repository to an AI coding agent. Read it before making changes. For
human-facing detail, follow the links into `docs/`.

## What EDB is

EDB (Extended Database) is an **Arduino/embedded record store**: fixed-size records in
byte-addressable storage (internal EEPROM, external I2C EEPROM, SD, SPIFFS, FRAM, or plain RAM). The
library never touches hardware directly — the caller supplies **read/write handler functions**, so
the same code runs on any medium and on the host for tests.

- **Language/target:** C++11, must stay **AVR-safe** (≈2 KB RAM on an ATmega328). No large stack
  buffers, no RAM index that scales with record count, no 64-bit math on device code paths.
- **Current on-disk format is v3** (since 2.0.0): a redundant CRC-32 header plus framed, individually
  CRC-16'd record slots, O(1) tombstone delete, stable record ids, ring-FIFO mode, and batch append.
- **Host ecosystem:** Python tools for offline migration/repair, a FastAPI gateway, and a browser
  Manager UI. Several of these reimplement the v3 format in Python and **must stay byte-compatible**
  with `EDB.cpp` (see [Cross-language parity](#cross-language-parity-critical)).

The single source of truth for the format is `EDB.cpp` / `EDB.h`. Docs and Python mirrors follow it.

## Repository map

| Path | What it is |
|------|-----------|
| `EDB.h`, `EDB.cpp` | The library. v3 format, slots, header, batch/ring/stable-ids logic. **Core.** |
| `EDB_Crypto.h/.cpp` | Optional at-rest AEAD (ChaCha20-Poly1305). Core is crypto-agnostic; this is a separate layer. |
| `examples/` | Arduino sketches (`.ino`), one per backend/feature. `examples/README.md` indexes them. |
| `test/` | Native (host) Unity test suites, fixtures, datasets, and the benchmark. See `docs/TESTING.md`. |
| `tools/` | Host-side Python. `edb_v3_format.py` is the **single Python source of truth** for the v3 format (constants, CRCs, header/slot primitives); `edb_v3_io.py` (offline check/vacuum/grow) and the gateway both import it. Plus `edb_migrate.py`, `edb_make_v1.py`, `edb_check.py`, `edb_vacuum.py`, `edb_grow.py`, `gen_datasets.py`, and `test_*.py`. |
| `services/edb-gateway/` | FastAPI gateway + web Manager UI. Has its own `edb_v3.py`, backends, transports, and `tests/`. |
| `release/1.0.7/` | Frozen prior version, compiled by the test harness for cross-version compat/benchmark. |
| `scripts/check_api.py` | Asserts the public API surface exists across release trees. |
| `docs/` | Human docs (see the [index](#documentation) below). |
| `keywords.txt` | Arduino IDE syntax highlighting — must list public API names. |

## Build & test

The repo builds and tests **on the host** (no board needed). Native suites use a RAM-backed handler.

```bash
make test          # everything: native v3 + 1.0.7 compat + Python tool tests + API check
make native-test   # v3 native Unity suite only
make fixtures      # regenerate test/fixtures (deterministic)
```

`make` shells out to:
- **Native C++** (`test/Makefile`): `g++ -std=c++11 -Wall -Wextra -DEDB_TEST -DEDB_ENABLE_CRYPTO ...`.
- **Python tools** (`tools/test_*.py`): `python -m unittest discover -s tools -p "test_*.py"`.
- **API check:** `python scripts/check_api.py`.

**Windows note:** there is no `make` by default. Use `test/build.ps1` (auto-detects `g++`/`clang++`),
or invoke the compiler directly. The MinGW toolchain path contains spaces, so **quote it** in Bash:
`"$G" -std=c++11 ... ` where `$G` is the full path to `g++.exe`. Compile every `test/test_*.cpp`
together with `../EDB.cpp ../EDB_Crypto.cpp support/unity.c`.

**Gateway tests:** `cd services/edb-gateway && python -m pytest -q` (config in `pyproject.toml`).

Always run `make fixtures` / `python tools/gen_datasets.py` before the native suite if you changed
anything that affects fixtures or datasets — the C tests load shipped `.db`/`.bin` files.

## v3 format essentials

Full spec in [docs/FORMAT.md](docs/FORMAT.md). What an agent must know before editing storage code:

- **Header:** two 48-byte copies at the table start (96 bytes). Each publish targets the *stale*
  copy with `seq+1` and a fresh CRC-32; `open()` picks the highest-`seq` valid copy and self-heals
  the other. Never overwrite the live copy in place. Fields are **explicit little-endian
  `uint32_t`/`uint16_t`** so files are byte-identical across AVR/ESP32/host.
- **Slot:** `[status:1][payload:rec_size][crc16:2]`. `status` = never-used `0x00` / live / tombstone.
  CRC-16-CCITT over `status||payload`, verified on read (`EDB_VERIFY_ON_READ`) → `EDB_CORRUPT` on
  mismatch, isolated to that record.
- **Delete is O(1):** tombstone + intrusive free-list threaded through dead slots (`free_head` in
  header). Nothing shifts. `recno` is a **slot index**, not a durable id, and does not renumber.
- **Header counts (`n_live`/`n_slots`/`free_head`) are a cache; slots are ground truth.** This is what
  makes batch append + reconcile-on-open safe.
- **Modes (header flags):** stable ids (id in payload `[0..3]`), ring FIFO (overwrite oldest), batch
  (deferred header publish, dirty-bit reconciled on open). See flags `EDB_HDR_*` in `EDB.h`.

## Public API & status codes

Handlers come in two flavors — **byte** handlers (`read/write` one byte) or **buffer** handlers
(`read/write` a block, e.g. SD/SPIFFS). Construct `EDB` with the matching pair.

Core: `create`, `open`, `openOrCreate`, `appendRec` (+ `appendRec(rec, &recno)`), `readRec`,
`updateRec`, `deleteRec`, `insertRec`, `count`, `limit`, `clear`.
Iteration (slots are sparse): `firstRec` / `nextRec` / `isLive` — **not** `for r in 1..count()`.
Feature toggles: `enableStableIds`/`recordId`/`findRecById`, `enableRingMode`/`fifoFirstRec`/
`fifoNextRec`, `beginBatch`/`endBatch`/`batchActive`, `compact`.

`EDB_Status`: `EDB_OK`, `EDB_ERROR`, `EDB_OUT_OF_RANGE`, `EDB_TABLE_FULL`, `EDB_DELETED`,
`EDB_CORRUPT`, `EDB_NEEDS_MIGRATION`. Full reference: [docs/API.md](docs/API.md).

### Build flags (compile-time knobs)

- `EDB_VERIFY_ON_READ` (default 1) — verify per-record CRC on read.
- `EDB_HEADER_REDUNDANT` (default 1) — dual header; 0 = single (discouraged).
- `EDB_WRITE_IF_DIFFERENT` (default 1) — read-before-write skip, **byte handlers only** (never buffer
  handlers). A win only when writes ≫ reads (EEPROM/flash); a loss on RAM/FRAM.
- `EDB_TEST` — enables host test hooks (e.g. `setMallocFail`). `EDB_ENABLE_CRYPTO` — build the AEAD.

## Conventions & gotchas

- **AVR-safety is a hard constraint.** Before adding a stack buffer or wider arithmetic on a device
  path, check it fits ~2 KB RAM and avoids 64-bit math. Integrity metadata lives on disk, not RAM.
- **Byte vs buffer handlers:** optimizations like write-if-different apply to byte handlers only. Keep
  buffer-handler paths writing whole blocks.
- **Iterate with `firstRec`/`nextRec`.** Post-delete the live set is sparse; index loops are wrong.
- **`recno` ≠ identity.** For durable references use `enableStableIds()` + `findRecById()`.
- **Line endings:** this repo uses CRLF for `.ino` (and git normalizes on checkout). Do not let an
  editor rewrite a whole file's line endings — it produces a spurious full-file diff.
- **Deterministic fixtures/datasets.** `tools/gen_datasets.py` and `tools/generate_fixtures.py` are
  seeded and reproducible; committed `test/data/*.bin` and `test/fixtures/*.db` must match a fresh
  regeneration (`tools/test_datasets.py` guards this). Regenerate, don't hand-edit binaries.
- **Keep `keywords.txt` and `scripts/check_api.py` in sync** when you add/rename public API.
- **Docs mirror code.** If you change the format, the API, or behavior, update the relevant file in
  `docs/` and `CHANGELOG.md` in the same change.

## Cross-language parity (critical)

The v3 format lives in **two** implementations that must agree byte-for-byte:

1. `EDB.cpp` / `EDB.h` — the device (the ultimate source of truth).
2. `tools/edb_v3_format.py` — the single Python source of truth. Both host consumers import it:
   `tools/edb_v3_io.py` (offline tools) and `services/edb-gateway/edb_gateway/edb_v3.py` (gateway).
   The gateway bootstraps the repo's `tools/` onto `sys.path` to import it, so it runs from within
   the repo.

So on the Python side there is exactly one copy of the constants, CRC-32 (`0xEDB88320`), CRC-16-CCITT
(`0x1021`), header field offsets, and slot framing — change the format in **`EDB.cpp` and
`edb_v3_format.py` together**, never in a consumer. Re-run the parity tests: `tools/test_edb_v3_format.py`
(the shared module), the native suite (loads Python-migrated fixtures), and
`services/edb-gateway/tests/test_edb_v3.py` + `tools/test_datasets.py` (assert the C and Python
readers agree on the same bytes). Do **not** re-add format constants or CRC code to a consumer module.

## Host tools (all offline; copy the file back to the device after)

- `edb_migrate.py` — convert v1/v2 → v2 or v3 (`--to`, `--arch`). `edb_make_v1.py` — generate legacy
  v1 files to test the converter (`--from-dataset` reads a `gen_datasets.py` manifest).
- `edb_check.py` — validate a v3 file. `edb_vacuum.py` — repack tombstones. `edb_grow.py` — enlarge
  `table_size`. On-device, only `compact()` (free-list repair) exists; vacuum/grow are host-only.

## Gateway

`services/edb-gateway/` is a FastAPI service (REST + web Manager) with API-key auth, a CORS
allowlist, a host `FileBackend` for `.db` files, an optional PSK serial transport, and browser
end-to-end "blind" encryption. See [docs/GATEWAY.md](docs/GATEWAY.md).

## Documentation

`docs/FORMAT.md` (on-disk format) · `docs/API.md` (public API + flags) ·
`docs/ARCHITECTURE.md` (design, complexity) · `docs/ENCRYPTION.md` (AEAD, transport, browser) ·
`docs/MIGRATION.md` + `docs/UPGRADE.md` (legacy → v3) · `docs/TESTING.md` (suites & how to run) ·
`docs/BENCHMARK.md` (perf methodology) · `docs/GATEWAY.md` · `docs/FAQ.md` · `CHANGELOG.md`.
