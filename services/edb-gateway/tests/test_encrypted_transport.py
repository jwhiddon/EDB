import pytest
from edb_gateway.transports.encrypted import EncryptedTransport, SessionCipher, derive_session_key
from edb_gateway.transports.mock import MockTransport


def test_session_cipher_roundtrip():
    key = derive_session_key("test-token-12345678")
    cipher = SessionCipher(key)
    raw = cipher.encrypt(b'{"id":1,"cmd":"ping"}')
    plain = cipher.decrypt(raw)
    assert b"ping" in plain


@pytest.mark.asyncio
async def test_encrypted_transport_pair():
    inner = MockTransport()
    t = EncryptedTransport(inner, token="test-token-12345678")
    await t.open()
    assert t.encrypt_transport
    resp = await t.send({"id": 2, "cmd": "ping"})
    assert resp["status"] == "EDB_OK"
    line = t.encrypt_line('{"secret":"data"}')
    assert b"data" not in line
    await t.close()
