from __future__ import annotations

import asyncio
import json
from typing import Any

from ..protocol import parse_line, sanitize_for_log
from .base import Transport


class MockTransport(Transport):
    """Replays scripted responses for tests."""

    def __init__(self, scripts: list[tuple[dict[str, Any], dict[str, Any]]] | None = None) -> None:
        self._scripts = list(scripts or [])
        self._queue: list[dict[str, Any]] = []
        self._open = False
        self._encrypt = False

    def enqueue_response(self, response: dict[str, Any]) -> None:
        self._queue.append(response)

    @property
    def encrypt_transport(self) -> bool:
        return self._encrypt

    async def open(self) -> None:
        self._open = True

    async def close(self) -> None:
        self._open = False

    async def send(self, command: dict[str, Any]) -> dict[str, Any]:
        if not self._open:
            raise RuntimeError("transport closed")
        for req, resp in self._scripts:
            if req.get("cmd") == command.get("cmd"):
                return resp
        if self._queue:
            return self._queue.pop(0)
        cmd = command.get("cmd", "")
        cid = command.get("id", 0)
        if cmd == "ping":
            return {"id": cid, "status": "EDB_OK", "data": {"version": "mock"}}
        if cmd == "info":
            return {
                "id": cid,
                "status": "EDB_OK",
                "data": {
                    "tables": [
                        {"head_ptr": 0, "table_size": 8192, "rec_size": 8, "label": "main"}
                    ]
                },
            }
        if cmd == "count":
            return {"id": cid, "status": "EDB_OK", "data": {"count": 0}}
        if cmd == "limit":
            return {"id": cid, "status": "EDB_OK", "data": {"limit": 1024}}
        if cmd == "open":
            return {"id": cid, "status": "EDB_OK"}
        if cmd == "create":
            return {"id": cid, "status": "EDB_OK"}
        if cmd == "readRec":
            return {
                "id": cid,
                "status": "EDB_OK",
                "data": {"recno": command.get("recno"), "payload_b64": "AQAAAA==", "enc_version": 0},
            }
        if cmd in ("appendRec", "updateRec", "deleteRec", "insertRec", "clear"):
            return {"id": cid, "status": "EDB_OK"}
        if cmd == "pair":
            self._encrypt = True
            return {"id": cid, "status": "EDB_OK", "data": {"paired": True}}
        return {"id": cid, "status": "EDB_ERROR"}
