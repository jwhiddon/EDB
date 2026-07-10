# EDB Gateway and Database Manager

The EDB Gateway is a host-side FastAPI service that exposes EDB on microcontrollers over REST and a lightweight web manager UI. It proxies commands to firmware running the [EDB Serial Bridge](../examples/EDB_SerialBridge/) sketch.

## Quick start

```bash
cd services/edb-gateway
pip install -e ".[dev]"
uvicorn edb_gateway.main:app --host 127.0.0.1 --port 8765
```

Open the manager at [http://127.0.0.1:8765/manager](http://127.0.0.1:8765/manager).

API docs (OpenAPI): [http://127.0.0.1:8765/docs](http://127.0.0.1:8765/docs).

### Environment variables


| Variable               | Default     | Purpose                                                         |
| ---------------------- | ----------- | --------------------------------------------------------------- |
| `EDB_GATEWAY_HOST`     | `127.0.0.1` | Bind address                                                    |
| `EDB_GATEWAY_PORT`     | `8765`      | Listen port                                                     |
| `EDB_GATEWAY_INSECURE` | unset       | Set to `1` to allow plaintext transport on localhost (dev only) |




## Architecture

```
Browser (Web Crypto)  →  FastAPI Gateway  →  Serial/Encrypted Transport  →  EDB_SerialBridge  →  EDB  →  SD/EEPROM
```

The gateway **never decrypts** user record data when encryption is enabled. See [ENCRYPTION.md](ENCRYPTION.md).

## REST API

All paths are relative to the gateway base URL. Record bodies use `payload_b64` (base64). When `enc_version > 0`, payloads are **ciphertext**.

### Devices and connections


| Method   | Path                     | Body                                                                         | Description                                         |
| -------- | ------------------------ | ---------------------------------------------------------------------------- | --------------------------------------------------- |
| `GET`    | `/devices`               | —                                                                            | List serial ports                                   |
| `POST`   | `/connections`           | `{ "transport": "serial", "port": "COM3", "baud": 115200, "encrypt": true }` | Open connection (encrypted needs `EDB_GATEWAY_PSK`) |
| `DELETE` | `/connections/{id}`      | —                                                                            | Close connection                                    |
| `POST`   | `/connections/{id}/pair` | —                                                                            | Re-run the PSK handshake (auto-runs on open)        |


`POST /connections/{id}/unlock` returns **501** — unlock happens in the browser only.

### Tables (`head_ptr` = byte offset in storage)


| Method | Path                                        | Description                             |
| ------ | ------------------------------------------- | --------------------------------------- |
| `GET`  | `/connections/{id}/tables`                  | List configured tables                  |
| `POST` | `/connections/{id}/tables`                  | Create table                            |
| `POST` | `/connections/{id}/tables/{head_ptr}/open`  | Open existing table                     |
| `GET`  | `/connections/{id}/tables/{head_ptr}`       | Metadata: count, limit, rec_size, enc_* |
| `POST` | `/connections/{id}/tables/{head_ptr}/clear` | Wipe table (destructive)                |


**Create body example:**

```json
{
  "head_ptr": 0,
  "table_size": 8192,
  "rec_size": 8,
  "enc_version": 0,
  "enc_mode": "e2e_blind",
  "plaintext_rec_size": 8
}
```

When `enc_version > 0`, `rec_size` should be `plaintext_rec_size + 16` (Poly1305 tag).

### Records (1-based `recno`, matching [API.md](API.md))


| Method   | Path                                                         | Description                           |
| -------- | ------------------------------------------------------------ | ------------------------------------- |
| `GET`    | `/connections/{id}/tables/{head_ptr}/records`                | List (`offset`, `limit` query params) |
| `GET`    | `/connections/{id}/tables/{head_ptr}/records/{recno}`        | Read one                              |
| `POST`   | `/connections/{id}/tables/{head_ptr}/records`                | Append                                |
| `PUT`    | `/connections/{id}/tables/{head_ptr}/records/{recno}`        | Update                                |
| `DELETE` | `/connections/{id}/tables/{head_ptr}/records/{recno}`        | Delete                                |
| `POST`   | `/connections/{id}/tables/{head_ptr}/records/{recno}/insert` | Insert (shift)                        |


**Append body:**

```json
{ "payload_b64": "AQAAAA==" }
```



### HTTP status mapping (`EDB_Status`)


| `EDB_Status`       | HTTP | Notes                                       |
| ------------------ | ---- | ------------------------------------------- |
| `EDB_OK`           | 200  | Success; body includes `"status": "EDB_OK"` |
| `EDB_ERROR`        | 400  | Invalid header, I/O failure, malloc failure |
| `EDB_OUT_OF_RANGE` | 422  | Invalid `recno`                             |
| `EDB_TABLE_FULL`   | 409  | No room for append/insert                   |


Connection errors (serial timeout, device offline) return **503**.

## Serial protocol (NDJSON)

One JSON object per line, one response per request. Correlated by `id`.

### Request

```json
{"id": 1, "cmd": "readRec", "head_ptr": 0, "recno": 2}
```



### Response

```json
{"id": 1, "status": "EDB_OK", "data": {"recno": 2, "payload_b64": "...", "enc_version": 0}}
```



### Commands


| `cmd`       | Fields                               | Maps to                     |
| ----------- | ------------------------------------ | --------------------------- |
| `ping`      | —                                    | Health check                |
| `pair`      | `token`                              | Transport session bootstrap |
| `info`      | —                                    | Bridge version, table list  |
| `open`      | `head_ptr`                           | `EDB::open`                 |
| `create`    | `head_ptr`, `table_size`, `rec_size` | `EDB::create`               |
| `count`     | `head_ptr`                           | `EDB::count`                |
| `limit`     | `head_ptr`                           | `EDB::limit`                |
| `readRec`   | `head_ptr`, `recno`                  | `EDB::readRec`              |
| `appendRec` | `head_ptr`, `payload_b64`            | `EDB::appendRec`            |
| `updateRec` | `head_ptr`, `recno`, `payload_b64`   | `EDB::updateRec`            |
| `deleteRec` | `head_ptr`, `recno`                  | `EDB::deleteRec`            |
| `compact`   | `head_ptr`                           | `EDB::compact`              |
| `insertRec` | `head_ptr`, `recno`, `payload_b64`   | `EDB::insertRec`            |
| `clear`     | `head_ptr`                           | `EDB::clear`                |


After `pair`, line payloads may be wrapped in a transport cipher (see [ENCRYPTION.md](ENCRYPTION.md)).

## Pairing flow

1. Flash [EDB_SerialBridge](../examples/EDB_SerialBridge/) on ESP32 + SD, built with
  `-DEDB_ENABLE_CRYPTO` and a `EDB_BRIDGE_PSK` matching the gateway's `EDB_GATEWAY_PSK`.
2. Gateway `POST /connections` with `encrypt: true` (requires `EDB_GATEWAY_PSK` to be set).
3. Gateway and device exchange public nonces and derive a session key from the shared PSK; the
  gateway verifies the device's `confirm` value.
4. Subsequent serial lines are ChaCha20-Poly1305 encrypted with per-direction counters. See
  [ENCRYPTION.md](ENCRYPTION.md) § Transport.
5. User opens `/manager`, enters passphrase locally (never sent to server).



## Dev mode (plaintext transport)

For bring-up without transport crypto:

1. Set `EDB_GATEWAY_INSECURE=1`
2. Connect with `"encrypt": false`
3. Bind must be `127.0.0.1`

Record payloads may still be ciphertext at the E2E layer when `enc_version > 0`.

## File backend (host-side .db files)

Instead of relaying to a serial device, the gateway can operate directly on a v3 `.db` file on the host — useful for inspecting or editing files migrated with`tools/edb_migrate.py`. The file format is byte-compatible with the firmware, so the same file works on either.

1. Set `EDB_GATEWAY_FILE_ROOT` to a directory holding your `.db` files. The backend is disabled until this is set, and all paths are confined to it (no traversal outside the root).
2. Open a connection with a file path instead of a serial port:
  ```
   POST /connections   { "backend": "file", "path": "events.db" }
  ```
3. Use the same table/record endpoints as a device connection.



## Related docs

- [API.md](API.md) — Arduino library API
- [ENCRYPTION.md](ENCRYPTION.md) — Threat model and crypto layers
- [FORMAT.md](FORMAT.md) — On-disk layout including encryption extension
- [TESTING.md](TESTING.md) — Running gateway and crypto tests

