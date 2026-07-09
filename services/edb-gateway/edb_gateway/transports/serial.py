from __future__ import annotations

import asyncio
import json
import logging
from typing import Any

from ..config import COMMAND_TIMEOUT_S
from ..protocol import sanitize_for_log
from .base import Transport

logger = logging.getLogger(__name__)

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # pragma: no cover
    serial = None
    list_ports = None


def list_serial_ports() -> list[dict[str, str]]:
    if list_ports is None:
        return []
    return [{"port": p.device, "description": p.description or ""} for p in list_ports.comports()]


class SerialTransport(Transport):
    def __init__(self, port: str, baud: int = 115200) -> None:
        self._port = port
        self._baud = baud
        self._ser = None
        self._lock = asyncio.Lock()
        self._next_id = 1
        self._encrypt = False

    @property
    def encrypt_transport(self) -> bool:
        return self._encrypt

    async def open(self) -> None:
        if serial is None:
            raise RuntimeError("pyserial not installed")
        self._ser = serial.Serial(self._port, self._baud, timeout=COMMAND_TIMEOUT_S)

    async def close(self) -> None:
        if self._ser and self._ser.is_open:
            self._ser.close()
        self._ser = None

    async def send(self, command: dict[str, Any]) -> dict[str, Any]:
        if not self._ser or not self._ser.is_open:
            raise RuntimeError("serial port closed")
        async with self._lock:
            cid = command.get("id")
            if cid is None:
                cid = self._next_id
                self._next_id += 1
                command = {**command, "id": cid}
            line = json.dumps(command, separators=(",", ":")) + "\n"
            logger.debug("serial tx %s", sanitize_for_log(command))
            await asyncio.to_thread(self._ser.write, line.encode())
            resp_line = await asyncio.to_thread(self._ser.readline)
            if not resp_line:
                raise TimeoutError("serial timeout")
            text = resp_line.decode(errors="replace").strip()
            logger.debug("serial rx %s", sanitize_for_log(json.loads(text)))
            return json.loads(text)
