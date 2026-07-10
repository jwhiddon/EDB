import base64
import json

import pytest

from edb_gateway.transports.base import Transport
from edb_gateway.transports.encrypted import (
    DIR_DEVICE,
    DIR_HOST,
    EncryptedTransport,
    SessionCipher,
    derive_session_key,
    session_confirm,
)


def _b64(b):
    return base64.b64encode(b).decode()


def _b64d(s):
    return base64.b64decode(s)


class DeviceSim(Transport):
    """In-process model of the Serial Bridge's device side of the PSK session protocol."""

    def __init__(self, psk: bytes, dev_nonce: bytes = bytes(range(12))) -> None:
        self._psk = psk
        self._dev_nonce = dev_nonce
        self._sk = None
        self._rx = -1
        self._tx = 0

    async def open(self) -> None:
        pass

    async def close(self) -> None:
        pass

    @property
    def encrypt_transport(self) -> bool:
        return self._sk is not None

    async def send(self, wrapped: dict) -> dict:
        if wrapped.get("cmd") == "pair":
            host_nonce = _b64d(wrapped["hnonce"])
            self._sk = derive_session_key(self._psk, host_nonce, self._dev_nonce)
            return {
                "id": wrapped.get("id"),
                "status": "EDB_OK",
                "data": {"dnonce": _b64(self._dev_nonce), "confirm": _b64(session_confirm(self._sk))},
            }
        if "enc" in wrapped:
            cipher = SessionCipher(self._sk)
            direction, counter, plaintext = cipher.open(_b64d(wrapped["enc"]))
            assert direction == DIR_HOST and counter > self._rx
            self._rx = counter
            command = json.loads(plaintext)
            resp = {"id": command.get("id"), "status": "EDB_OK", "data": {"echo": command.get("cmd")}}
            framed = cipher.seal(DIR_DEVICE, self._tx, json.dumps(resp).encode())
            self._tx += 1
            return {"enc": _b64(framed)}
        return {"status": "EDB_ERROR"}


@pytest.mark.asyncio
async def test_transport_session_roundtrip():
    psk = bytes(range(32))
    t = EncryptedTransport(DeviceSim(psk), psk=psk)
    await t.open()  # auto-pairs
    assert t.encrypt_transport
    resp = await t.send({"id": 5, "cmd": "ping"})
    assert resp["status"] == "EDB_OK"
    assert resp["data"]["echo"] == "ping"
    resp2 = await t.send({"id": 6, "cmd": "count"})
    assert resp2["data"]["echo"] == "count"


@pytest.mark.asyncio
async def test_wrong_psk_fails_confirmation():
    t = EncryptedTransport(DeviceSim(bytes(range(32))), psk=bytes([9] * 32))
    with pytest.raises(RuntimeError):
        await t.open()


def test_session_kdf_matches_c_kat():
    # Same vector asserted byte-for-byte by the native C test (test_session_transport_kat).
    psk = bytes([0x11] * 32)
    sk = derive_session_key(psk, bytes(range(12)), bytes(range(0x10, 0x1C)))
    assert sk.hex() == "b6700e5e515e4b74603e790a87af8105699d4e2bca4ad5185dad68e4c176b193"
    assert session_confirm(sk).hex() == "b52769ea7a9a86d2412369b97370c2c0"


def test_line_framing_tamper_rejected():
    cipher = SessionCipher(bytes(range(32)))
    framed = bytearray(cipher.seal(DIR_HOST, 0, b'{"cmd":"ping"}'))
    framed[-1] ^= 0x01  # corrupt the tag
    with pytest.raises(Exception):
        cipher.open(bytes(framed))
