from __future__ import annotations

from typing import Any

from ..models import ProtocolResponse
from ..transports import Transport


class DeviceBackend:
    def __init__(self, transport: Transport) -> None:
        self._transport = transport
        self._next_id = 1

    async def _cmd(self, cmd: str, **kwargs: Any) -> ProtocolResponse:
        msg_id = self._next_id
        self._next_id += 1
        payload = {"id": msg_id, "cmd": cmd, **kwargs}
        raw = await self._transport.send(payload)
        return ProtocolResponse.from_dict(raw)

    async def ping(self) -> ProtocolResponse:
        return await self._cmd("ping")

    async def info(self) -> ProtocolResponse:
        return await self._cmd("info")

    async def open_table(self, head_ptr: int) -> ProtocolResponse:
        return await self._cmd("open", head_ptr=head_ptr)

    async def create_table(self, head_ptr: int, table_size: int, rec_size: int) -> ProtocolResponse:
        return await self._cmd("create", head_ptr=head_ptr, table_size=table_size, rec_size=rec_size)

    async def count(self, head_ptr: int) -> ProtocolResponse:
        return await self._cmd("count", head_ptr=head_ptr)

    async def limit(self, head_ptr: int) -> ProtocolResponse:
        return await self._cmd("limit", head_ptr=head_ptr)

    async def clear(self, head_ptr: int) -> ProtocolResponse:
        return await self._cmd("clear", head_ptr=head_ptr)

    async def read_rec(self, head_ptr: int, recno: int) -> ProtocolResponse:
        return await self._cmd("readRec", head_ptr=head_ptr, recno=recno)

    async def append_rec(self, head_ptr: int, payload_b64: str) -> ProtocolResponse:
        return await self._cmd("appendRec", head_ptr=head_ptr, payload_b64=payload_b64)

    async def update_rec(self, head_ptr: int, recno: int, payload_b64: str) -> ProtocolResponse:
        return await self._cmd("updateRec", head_ptr=head_ptr, recno=recno, payload_b64=payload_b64)

    async def delete_rec(self, head_ptr: int, recno: int) -> ProtocolResponse:
        return await self._cmd("deleteRec", head_ptr=head_ptr, recno=recno)

    async def insert_rec(self, head_ptr: int, recno: int, payload_b64: str) -> ProtocolResponse:
        return await self._cmd("insertRec", head_ptr=head_ptr, recno=recno, payload_b64=payload_b64)
