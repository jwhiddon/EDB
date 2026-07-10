import pytest
from edb_gateway.transports.mock import MockTransport


@pytest.mark.asyncio
async def test_mock_ping():
    t = MockTransport()
    await t.open()
    resp = await t.send({"id": 1, "cmd": "ping"})
    assert resp["status"] == "EDB_OK"
    await t.close()


@pytest.mark.asyncio
async def test_mock_read_rec():
    t = MockTransport()
    await t.open()
    resp = await t.send({"id": 2, "cmd": "readRec", "head_ptr": 0, "recno": 1})
    assert resp["data"]["payload_b64"]
    await t.close()
