# EDB Serial Bridge

Firmware that exposes EDB over newline-delimited JSON on serial. Used with the [EDB Gateway](../../services/edb-gateway/).

## Hardware

- **Tier 1 (recommended):** ESP32 + SD card (SPI CS on GPIO 5 by default)
- **Tier 2:** Arduino Mega + SD — set `EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO=0` in build flags

## Flash

```bash
arduino-cli compile --library ../.. --fqbn esp32:esp32:esp32 examples/EDB_SerialBridge
arduino-cli upload --fqbn esp32:esp32:esp32 -p COM3 examples/EDB_SerialBridge
```

## Table configuration

Edit `EDB_SerialBridge.ino` — `tables[]` lists `head_ptr`, `table_size`, `rec_size`, and label.

## Build flags

Both crypto features require the global `-DEDB_ENABLE_CRYPTO` build flag (so `EDB_Crypto.cpp` is
compiled). Without it the bridge runs in plaintext. See [docs/ENCRYPTION.md](../../docs/ENCRYPTION.md)
and `config.h`.

| Flag | Purpose |
|------|---------|
| `EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO` | PSK session encryption on the serial wire (ESP32 default on). Set the matching `EDB_BRIDGE_PSK`. |
| `EDB_BRIDGE_ENABLE_AT_REST_CRYPTO` | Device seals/opens records with `EDB_BRIDGE_AT_REST_KEY`; tables use `rec_size = plaintext + 28`. |
| `EDB_ENABLE_CRYPTO` | Global build flag enabling the crypto library (required by both features above). |
| `EDB_BRIDGE_MAX_LINE` | RX/TX buffer size. |

Compile with transport crypto:

```bash
arduino-cli compile --library ../.. --fqbn esp32:esp32:esp32 \
  --build-property "compiler.cpp.extra_flags=-DEDB_ENABLE_CRYPTO" examples/EDB_SerialBridge
```

## Protocol

See [docs/GATEWAY.md](../../docs/GATEWAY.md).
