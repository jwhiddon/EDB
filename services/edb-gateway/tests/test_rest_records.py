import pytest
from httpx import ASGITransport, AsyncClient

from edb_gateway.main import app


@pytest.fixture
async def client():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as c:
        yield c


@pytest.fixture
async def conn_id(client):
    r = await client.post("/connections", json={"port": "mock", "mock": True, "encrypt": False})
    return r.json()["id"]


@pytest.mark.asyncio
async def test_append_and_read(client, conn_id):
    await client.post(
        f"/connections/{conn_id}/tables",
        json={"head_ptr": 0, "table_size": 8192, "rec_size": 8},
    )
    r = await client.post(
        f"/connections/{conn_id}/tables/0/records",
        json={"payload_b64": "AQAAAA=="},
    )
    assert r.json()["status"] == "EDB_OK"
    r2 = await client.get(f"/connections/{conn_id}/tables/0/records/1")
    assert r2.status_code == 200


@pytest.mark.asyncio
async def test_list_records_pagination(client, conn_id):
    r = await client.get(f"/connections/{conn_id}/tables/0/records?limit=10")
    assert r.status_code == 200
