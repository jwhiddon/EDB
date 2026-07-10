from __future__ import annotations

import hmac

from fastapi import Header, HTTPException

from . import config


def require_api_key(x_api_key: str | None = Header(default=None)) -> None:
    """Enforce the shared-secret API key when one is configured.

    If EDB_GATEWAY_API_KEY is unset the gateway runs in loopback-only development mode
    (see main.py, which refuses a non-loopback bind without a key). When it is set, every
    request must carry a matching X-API-Key header, compared in constant time.
    """
    expected = config.API_KEY
    if not expected:
        return
    if not x_api_key or not hmac.compare_digest(x_api_key, expected):
        raise HTTPException(status_code=401, detail="missing or invalid API key")
