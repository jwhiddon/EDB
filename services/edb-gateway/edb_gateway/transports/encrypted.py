from __future__ import annotations

import base64
import hmac
import json
import os
from typing import Any

from cryptography.hazmat.primitives.ciphers.aead import ChaCha20Poly1305

from .base import Transport

NONCE_LEN = 12
KEY_LEN = 32
DIR_HOST = 0x01
DIR_DEVICE = 0x02


def _b64(data: bytes) -> str:
    return base64.b64encode(data).decode("ascii")


def _b64d(text: str) -> bytes:
    return base64.b64decode(text)


def _keystream32(key: bytes, nonce: bytes) -> bytes:
    # ChaCha20-Poly1305 encrypts plaintext starting at block counter 1, so encrypting 32 zero
    # bytes yields keystream[counter=1][0:32]. Matches edb_chacha20_block(key, nonce, 1)[0:32].
    return ChaCha20Poly1305(key).encrypt(nonce, b"\x00" * 32, b"")[:32]


def derive_session_key(psk: bytes, host_nonce: bytes, dev_nonce: bytes) -> bytes:
    k0 = _keystream32(psk, host_nonce)
    k1 = _keystream32(psk, dev_nonce)
    return bytes(a ^ b for a, b in zip(k0, k1))


def session_confirm(session_key: bytes) -> bytes:
    return ChaCha20Poly1305(session_key).encrypt(b"\x00" * NONCE_LEN, b"\x00" * 16, b"")[:16]


def _line_nonce(direction: int, counter: int) -> bytes:
    return bytes([direction]) + counter.to_bytes(8, "little") + b"\x00\x00\x00"


class SessionCipher:
    """ChaCha20-Poly1305 line framing: nonce(12) || ciphertext || tag(16)."""

    def __init__(self, key: bytes) -> None:
        self._aead = ChaCha20Poly1305(key)

    def seal(self, direction: int, counter: int, plaintext: bytes) -> bytes:
        nonce = _line_nonce(direction, counter)
        return nonce + self._aead.encrypt(nonce, plaintext, b"")

    def open(self, framed: bytes) -> tuple[int, int, bytes]:
        if len(framed) < NONCE_LEN + 16:
            raise ValueError("frame too short")
        nonce, body = framed[:NONCE_LEN], framed[NONCE_LEN:]
        plaintext = self._aead.decrypt(nonce, body, b"")
        return nonce[0], int.from_bytes(nonce[1:9], "little"), plaintext


class EncryptedTransport(Transport):
    """PSK session over the inner transport.

    Handshake (cleartext nonces): host sends a random `hnonce`; the device replies with its
    `dnonce` and a `confirm` value. Both sides derive the session key from the shared PSK; the
    host verifies `confirm` to detect a wrong/absent PSK. Every subsequent command/response line
    is ChaCha20-Poly1305 encrypted with a per-direction message counter.
    """

    def __init__(self, inner: Transport, psk: bytes | None = None) -> None:
        self._inner = inner
        self._psk = psk
        self._cipher: SessionCipher | None = None
        self._paired = False
        self._tx = 0
        self._rx = -1

    @property
    def encrypt_transport(self) -> bool:
        return self._paired

    async def open(self) -> None:
        await self._inner.open()
        if self._psk:
            await self.pair()

    async def pair(self, token: str | None = None) -> None:  # token kept for API compatibility
        if not self._psk:
            raise RuntimeError("transport encryption requires a PSK (EDB_GATEWAY_PSK)")
        host_nonce = os.urandom(NONCE_LEN)
        resp = await self._inner.send({"id": 1, "cmd": "pair", "hnonce": _b64(host_nonce)})
        if resp.get("status") != "EDB_OK":
            raise RuntimeError("pair rejected by device")
        data = resp.get("data") or {}
        dev_nonce = _b64d(data["dnonce"])
        confirm = _b64d(data["confirm"])
        session_key = derive_session_key(self._psk, host_nonce, dev_nonce)
        if not hmac.compare_digest(session_confirm(session_key), confirm):
            raise RuntimeError("pairing confirmation failed (wrong PSK?)")
        self._cipher = SessionCipher(session_key)
        self._paired = True
        self._tx = 0
        self._rx = -1

    async def close(self) -> None:
        await self._inner.close()
        self._cipher = None
        self._paired = False

    async def send(self, command: dict[str, Any]) -> dict[str, Any]:
        if not self._paired or self._cipher is None:
            raise RuntimeError("transport not paired")
        line = json.dumps(command, separators=(",", ":")).encode()
        framed = self._cipher.seal(DIR_HOST, self._tx, line)
        self._tx += 1
        resp = await self._inner.send({"enc": _b64(framed)})
        if "enc" not in resp:
            raise RuntimeError("expected encrypted response")
        direction, counter, plaintext = self._cipher.open(_b64d(resp["enc"]))
        if direction != DIR_DEVICE or counter <= self._rx:
            raise RuntimeError("bad response frame (replay or wrong direction)")
        self._rx = counter
        return json.loads(plaintext)
