from __future__ import annotations

import os
import uuid
from dataclasses import dataclass
from typing import Any

from . import config
from .backends.device import DeviceBackend
from .config import insecure_allowed
from .transports import EncryptedTransport, MockTransport, SerialTransport, Transport


@dataclass
class Connection:
    id: str
    transport: Transport | None
    backend: Any
    encrypt: bool = True


def _safe_file_path(root: str, rel: str) -> str:
    root_abs = os.path.realpath(root)
    full = os.path.realpath(os.path.join(root_abs, rel))
    if os.path.commonpath([root_abs, full]) != root_abs:
        raise PermissionError("path escapes EDB_GATEWAY_FILE_ROOT")
    return full


class ConnectionManager:
    def __init__(self) -> None:
        self._connections: dict[str, Connection] = {}

    def get(self, conn_id: str) -> Connection | None:
        return self._connections.get(conn_id)

    async def create_serial(
        self,
        port: str,
        baud: int = 115200,
        encrypt: bool = True,
        mock: bool = False,
    ) -> Connection:
        if encrypt:
            if config.PSK is None:
                raise PermissionError(
                    "transport encryption requires EDB_GATEWAY_PSK (or set encrypt=false)"
                )
            inner: Transport = MockTransport() if mock else SerialTransport(port, baud)
            transport: Transport = EncryptedTransport(inner, psk=config.PSK)
        else:
            if not insecure_allowed():
                raise PermissionError("plaintext transport requires EDB_GATEWAY_INSECURE=1")
            transport = MockTransport() if mock else SerialTransport(port, baud)
        await transport.open()
        conn_id = str(uuid.uuid4())
        backend = DeviceBackend(transport)
        conn = Connection(id=conn_id, transport=transport, backend=backend, encrypt=encrypt)
        self._connections[conn_id] = conn
        return conn

    async def create_mock(self, encrypt: bool = False) -> Connection:
        return await self.create_serial(port="mock", encrypt=encrypt, mock=True)

    async def create_file(self, path: str) -> Connection:
        from .backends.file import FileBackend

        if not config.FILE_ROOT:
            raise PermissionError("file backend disabled; set EDB_GATEWAY_FILE_ROOT")
        if not path:
            raise ValueError("file path required")
        full = _safe_file_path(config.FILE_ROOT, path)
        conn_id = str(uuid.uuid4())
        conn = Connection(id=conn_id, transport=None, backend=FileBackend(full), encrypt=False)
        self._connections[conn_id] = conn
        return conn

    async def close(self, conn_id: str) -> None:
        conn = self._connections.pop(conn_id, None)
        if conn:
            if conn.transport is not None:
                await conn.transport.close()
            elif hasattr(conn.backend, "close"):
                await conn.backend.close()


connections = ConnectionManager()
