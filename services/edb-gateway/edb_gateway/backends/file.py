"""File backend: the gateway operates directly on a host-side v3 .db file.

Exposes the same async surface as DeviceBackend, but instead of relaying NDJSON to a device it
reads/writes the file with edb_v3 (byte-compatible with the firmware format).
"""

from __future__ import annotations

import base64

from ..edb_v3 import ERROR, OK, EdbV3File, OpenError
from ..models import EdbStatus, ProtocolResponse


def _resp(status: str, data: dict | None = None) -> ProtocolResponse:
    return ProtocolResponse(id=0, status=EdbStatus(status), data=data or {})


class FileBackend:
    def __init__(self, path: str) -> None:
        self._db = EdbV3File(path)

    async def info(self) -> ProtocolResponse:
        try:
            header, _ = self._db._active(0)
            tables = [{
                "head_ptr": 0,
                "table_size": header.table_size,
                "rec_size": header.rec_size,
                "label": "file",
            }]
        except OpenError:
            tables = []
        return _resp(OK, {"tables": tables})

    async def open_table(self, head_ptr: int) -> ProtocolResponse:
        return _resp(self._db.open(head_ptr))

    async def create_table(self, head_ptr: int, table_size: int, rec_size: int) -> ProtocolResponse:
        return _resp(self._db.create(head_ptr, table_size, rec_size))

    async def count(self, head_ptr: int) -> ProtocolResponse:
        try:
            return _resp(OK, {"count": self._db.count(head_ptr)})
        except OpenError as exc:
            return _resp(exc.status)

    async def limit(self, head_ptr: int) -> ProtocolResponse:
        try:
            return _resp(OK, {"limit": self._db.limit(head_ptr)})
        except OpenError as exc:
            return _resp(exc.status)

    async def clear(self, head_ptr: int) -> ProtocolResponse:
        try:
            return _resp(self._db.clear(head_ptr))
        except OpenError as exc:
            return _resp(exc.status)

    async def read_rec(self, head_ptr: int, recno: int) -> ProtocolResponse:
        try:
            status, payload = self._db.read_rec(head_ptr, recno)
        except OpenError as exc:
            return _resp(exc.status)
        if status != OK:
            return _resp(status)
        return _resp(OK, {
            "recno": recno,
            "payload_b64": base64.b64encode(payload).decode("ascii"),
            "enc_version": 0,
        })

    async def append_rec(self, head_ptr: int, payload_b64: str) -> ProtocolResponse:
        try:
            status, recno = self._db.append_rec(head_ptr, base64.b64decode(payload_b64))
        except OpenError as exc:
            return _resp(exc.status)
        return _resp(status, {"recno": recno} if status == OK else None)

    async def update_rec(self, head_ptr: int, recno: int, payload_b64: str) -> ProtocolResponse:
        try:
            return _resp(self._db.update_rec(head_ptr, recno, base64.b64decode(payload_b64)))
        except OpenError as exc:
            return _resp(exc.status)

    async def delete_rec(self, head_ptr: int, recno: int) -> ProtocolResponse:
        try:
            return _resp(self._db.delete_rec(head_ptr, recno))
        except OpenError as exc:
            return _resp(exc.status)

    async def insert_rec(self, head_ptr: int, recno: int, payload_b64: str) -> ProtocolResponse:
        try:
            status, rid = self._db.insert_rec(head_ptr, recno, base64.b64decode(payload_b64))
        except OpenError as exc:
            return _resp(exc.status)
        return _resp(status, {"recno": rid} if status == OK else None)

    async def close(self) -> None:
        pass
