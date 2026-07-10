# EDB Gateway

Host-side FastAPI service for managing EDB on microcontrollers.

## Install

```bash
pip install -e ".[dev]"
```

## Run

```bash
uvicorn edb_gateway.main:app --host 127.0.0.1 --port 8765
```

Manager UI: http://127.0.0.1:8765/manager

## Environment

| Variable | Default |
|----------|---------|
| `EDB_GATEWAY_HOST` | `127.0.0.1` |
| `EDB_GATEWAY_PORT` | `8765` |
| `EDB_GATEWAY_INSECURE` | unset — set to `1` for plaintext transport (localhost only) |

## Tests

```bash
pytest
```

See [docs/GATEWAY.md](../../docs/GATEWAY.md).
