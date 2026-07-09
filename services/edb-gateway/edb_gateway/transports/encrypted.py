from __future__ import annotations

import json
from typing import Any

from .base import Transport


def derive_session_key(token: str) -> bytes:
    import hashlib

    return hashlib.sha256(f"edb-session:{token}".encode()).digest()


class SessionCipher:
    """ChaCha20-Poly1305 line envelope (PyNaCl SecretBox) for tests and future wire wrapping."""

    def __init__(self, key: bytes) -> None:
        import nacl.secret

        self._box = nacl.secret.SecretBox(key)

    def encrypt(self, plaintext: bytes) -> bytes:
        return self._box.encrypt(plaintext)

    def decrypt(self, data: bytes) -> bytes:
        return self._box.decrypt(data)


class EncryptedTransport(Transport):
    """Session pairing + optional line cipher (inner passthrough for serial NDJSON)."""

    def __init__(self, inner: Transport, token: str | None = None) -> None:
        self._inner = inner
        self._token = token
        self._cipher: SessionCipher | None = None
        self._paired = False

    @property
    def encrypt_transport(self) -> bool:
        return self._paired

    async def open(self) -> None:
        await self._inner.open()
        if self._token:
            await self.pair(self._token)

    async def pair(self, token: str) -> None:
        resp = await self._inner.send({"id": 1, "cmd": "pair", "token": token})
        if resp.get("status") != "EDB_OK":
            raise RuntimeError("pair failed")
        self._cipher = SessionCipher(derive_session_key(token))
        self._paired = True

    async def close(self) -> None:
        await self._inner.close()
        self._cipher = None
        self._paired = False

    async def send(self, command: dict[str, Any]) -> dict[str, Any]:
        return await self._inner.send(command)

    def encrypt_line(self, line: str) -> bytes:
        if not self._cipher:
            raise RuntimeError("not paired")
        return self._cipher.encrypt(line.encode())

    def decrypt_line(self, data: bytes) -> str:
        if not self._cipher:
            raise RuntimeError("not paired")
        return self._cipher.decrypt(data).decode()
