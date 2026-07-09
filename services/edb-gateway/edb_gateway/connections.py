from __future__ import annotations

import uuid
from dataclasses import dataclass

from . import config
from .backends.device import DeviceBackend
from .config import insecure_allowed
from .transports import EncryptedTransport, MockTransport, SerialTransport, Transport


@dataclass
class Connection:
    id: str
    transport: Transport
    backend: DeviceBackend
    encrypt: bool = True


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
        pairing_token: str | None = None,
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

    async def close(self, conn_id: str) -> None:
        conn = self._connections.pop(conn_id, None)
        if conn:
            await conn.transport.close()


connections = ConnectionManager()
