from __future__ import annotations

import base64
import json
import logging
from typing import Any

from .models import ProtocolMessage, ProtocolResponse

logger = logging.getLogger(__name__)

# Redact sensitive keys from debug logs
_SENSITIVE = frozenset({"payload_b64", "token", "pairing_token", "passphrase"})


def sanitize_for_log(obj: Any) -> Any:
    if isinstance(obj, dict):
        return {k: ("<redacted>" if k in _SENSITIVE else sanitize_for_log(v)) for k, v in obj.items()}
    if isinstance(obj, list):
        return [sanitize_for_log(x) for x in obj]
    return obj


def encode_line(msg: ProtocolMessage) -> str:
    return json.dumps(msg.to_wire(), separators=(",", ":"))


def parse_line(line: str) -> ProtocolResponse:
    data = json.loads(line.strip())
    return ProtocolResponse.from_dict(data)


def b64_encode(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def b64_decode(s: str) -> bytes:
    return base64.b64decode(s.encode("ascii"))
