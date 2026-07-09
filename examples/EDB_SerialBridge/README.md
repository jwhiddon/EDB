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

See [docs/ENCRYPTION.md](../../docs/ENCRYPTION.md) and `config.h`.

| Flag | Purpose |
|------|---------|
| `EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO` | Session encryption on serial |
| `EDB_BRIDGE_MAX_LINE` | RX buffer size |
| `EDB_ENABLE_CRYPTO` | Include at-rest crypto handlers |
| `EDB_CRYPTO_DEVICE_AUTONOMOUS` | On-device encrypt for logging |

## Protocol

See [docs/GATEWAY.md](../../docs/GATEWAY.md).
