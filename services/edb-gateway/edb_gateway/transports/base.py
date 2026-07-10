from __future__ import annotations

import abc
from typing import Any


class Transport(abc.ABC):
    @abc.abstractmethod
    async def open(self) -> None: ...

    @abc.abstractmethod
    async def close(self) -> None: ...

    @abc.abstractmethod
    async def send(self, command: dict[str, Any]) -> dict[str, Any]: ...

    @property
    @abc.abstractmethod
    def encrypt_transport(self) -> bool: ...
