# Testing EDB

## Run everything

```bash
make test
cd services/edb-gateway && pip install -e ".[dev]" && pytest
```

On Windows without `make`, run the individual commands below.

## Native C++ tests

Host-native tests compile `EDB.cpp` with an in-memory fake storage backend.

```bash
make -C test test        # 2.0.0 library + crypto suites
make -C test test-107    # 1.0.7 drop-in library
make -C test test-crypto # explicit crypto target
```

On Windows with `g++` or `clang++` installed:

```powershell
powershell -File test/build.ps1
```

Requirements: `g++` with C++11 support.

### Test suites

| Suite | File | Purpose |
|-------|------|---------|
| Smoke | `test_edb.cpp` | Core API validation |
| Data integrity | `test_edb_integrity.cpp` | Full-table record preservation |
| Code integrity | `test_api_compat.cpp` | Regression cases + golden fixtures |
| Crypto | `test_crypto.cpp` | AEAD round-trip, wrong key |
| Crypto header | `test_crypto_header.cpp` | Encryption extension parse/write |
| Crypto integrity | `test_crypto_integrity.cpp` | Shift ops on ciphertext slots |
| Crypto blind | `test_crypto_blind.cpp` | Opaque byte storage |
| Compile flags | `test_compile_flags.cpp` | Optional feature guards |

### Data integrity coverage

- Shift operations (delete/insert at first, middle, last) verify **all** records
- Failed append/insert/malloc paths leave storage unchanged
- Reopen after mixed operations preserves data
- Byte vs buffer handler parity (identical on-disk bytes)
- v2-only: v1 AVR upgrade preserves records

### Code integrity coverage

- `recno == 0` returns `EDB_OUT_OF_RANGE` without mutating storage
- Corrupt `open()` returns `EDB_ERROR`
- Golden fixtures in `test/fixtures/` (generate with `make -C test fixtures`)
- API check: `python scripts/check_api.py`
- Crypto vectors: `test/fixtures/crypto_vectors.json`

## Gateway tests (Python)

```bash
cd services/edb-gateway
pip install -e ".[dev]"
pytest
```

Uses `MockTransport` — no hardware required. Covers protocol, REST, encrypted transport pairing, dev-mode guards, and log redaction.

### Optional hardware serial test

With ESP32 running `EDB_SerialBridge`:

```bash
EDB_GATEWAY_INSECURE=1 pytest -m serial   # when serial tests are added
```

## Migration tool tests

```bash
python -m unittest discover -s tools -p "test_*.py"
```

Includes record-region preservation, full-table migration, and padding checks.

## Arduino compile check

If `arduino-cli` is installed:

```bash
arduino-cli lib install "SD"
arduino-cli compile --fqbn arduino:avr:uno examples/EDB_Simple
arduino-cli compile --library . --fqbn esp32:esp32:esp32 examples/EDB_SerialBridge
```

## Continuous integration

GitHub Actions workflow `.github/workflows/test.yml` runs:

1. Native C++ tests (2.0.0) including crypto suites
2. Native C++ tests (1.0.7 drop-in)
3. Python migration tests
4. Public API compatibility check
5. Arduino example compiles (AVR + ESP32 SerialBridge)
6. Gateway pytest suite

## Testing without hardware

Host-native tests (`make -C test test`) and gateway pytest exercise library and service logic without an Arduino board. Use a real board or [Wokwi](https://wokwi.com) for final EEPROM/SD integration checks.
