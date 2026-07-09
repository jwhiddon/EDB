import os


def insecure_allowed() -> bool:
    return os.environ.get("EDB_GATEWAY_INSECURE", "") == "1"


HOST = os.environ.get("EDB_GATEWAY_HOST", "127.0.0.1")
PORT = int(os.environ.get("EDB_GATEWAY_PORT", "8765"))
COMMAND_TIMEOUT_S = float(os.environ.get("EDB_GATEWAY_TIMEOUT", "5.0"))

# Shared-secret API key. When set, every API route requires a matching X-API-Key
# header. When unset, requests are allowed only for loopback development (see main.py,
# which refuses to bind a non-loopback HOST without a key).
API_KEY = os.environ.get("EDB_GATEWAY_API_KEY") or None


def _split_env(name: str, default: str) -> list[str]:
    raw = os.environ.get(name, default)
    return [item.strip() for item in raw.split(",") if item.strip()]


# CORS: explicit allowlist only (never "*"). Default: the local manager origins.
ALLOWED_ORIGINS = _split_env(
    "EDB_GATEWAY_ALLOWED_ORIGINS",
    "http://localhost:8765,http://127.0.0.1:8765",
)

# Host header allowlist (DNS-rebinding defense). Includes the ASGI test hosts so the
# in-process test client works; operators add their real hostname via the env var.
ALLOWED_HOSTS = _split_env(
    "EDB_GATEWAY_ALLOWED_HOSTS",
    "localhost,127.0.0.1,test,testserver",
)


def host_is_loopback() -> bool:
    return HOST in ("127.0.0.1", "::1", "localhost")


def _psk() -> bytes | None:
    """32-byte transport pre-shared key (hex), shared with the device firmware."""
    raw = os.environ.get("EDB_GATEWAY_PSK")
    if not raw:
        return None
    try:
        key = bytes.fromhex(raw)
    except ValueError as exc:
        raise RuntimeError("EDB_GATEWAY_PSK must be hex") from exc
    if len(key) != 32:
        raise RuntimeError("EDB_GATEWAY_PSK must decode to 32 bytes (64 hex chars)")
    return key


PSK = _psk()
