import pytest
from httpx import ASGITransport, AsyncClient

from edb_gateway.main import app


@pytest.fixture
async def client():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as c:
        yield c


@pytest.mark.asyncio
async def test_create_mock_connection(client):
    r = await client.post("/connections", json={"port": "mock", "mock": True, "encrypt": False})
    assert r.status_code == 200
    assert "id" in r.json()


@pytest.mark.asyncio
async def test_list_tables(client):
    r = await client.post("/connections", json={"port": "mock", "mock": True, "encrypt": False})
    cid = r.json()["id"]
    r2 = await client.get(f"/connections/{cid}/tables")
    assert r2.status_code == 200
    assert r2.json()["status"] == "EDB_OK"


@pytest.mark.asyncio
async def test_unlock_not_server_side(client):
    r = await client.post("/connections", json={"port": "mock", "mock": True, "encrypt": False})
    cid = r.json()["id"]
    r2 = await client.post(f"/connections/{cid}/unlock")
    assert r2.status_code == 501
