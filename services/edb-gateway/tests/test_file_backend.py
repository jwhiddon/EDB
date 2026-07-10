import base64
import struct

import pytest
from httpx import ASGITransport, AsyncClient

from edb_gateway import config
from edb_gateway.main import app


@pytest.fixture
async def client():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as c:
        yield c


@pytest.mark.asyncio
async def test_file_backend_crud(client, tmp_path, monkeypatch):
    monkeypatch.setattr(config, "FILE_ROOT", str(tmp_path))
    r = await client.post("/connections", json={"backend": "file", "path": "t.db"})
    assert r.status_code == 200
    cid = r.json()["id"]

    r = await client.post(
        f"/connections/{cid}/tables",
        json={"head_ptr": 0, "table_size": 512, "rec_size": 4},
    )
    assert r.json()["status"] == "EDB_OK"

    payload = base64.b64encode(struct.pack("<i", 42)).decode()
    r = await client.post(f"/connections/{cid}/tables/0/records", json={"payload_b64": payload})
    assert r.json()["status"] == "EDB_OK"

    r = await client.get(f"/connections/{cid}/tables/0/records/1")
    body = r.json()
    assert body["status"] == "EDB_OK"
    got = base64.b64decode(body["data"]["payload_b64"])
    assert struct.unpack("<i", got)[0] == 42

    r = await client.get(f"/connections/{cid}/tables/0/records?limit=10")
    assert r.json()["data"]["total"] == 1

    # delete tombstones the slot; count drops
    r = await client.delete(f"/connections/{cid}/tables/0/records/1")
    assert r.json()["status"] == "EDB_OK"
    r = await client.get(f"/connections/{cid}/tables/0")
    assert r.json()["data"]["count"] == 0

    assert (tmp_path / "t.db").exists()


@pytest.mark.asyncio
async def test_file_backend_disabled_without_root(client, monkeypatch):
    monkeypatch.setattr(config, "FILE_ROOT", None)
    r = await client.post("/connections", json={"backend": "file", "path": "x.db"})
    assert r.status_code == 403


@pytest.mark.asyncio
async def test_file_backend_path_traversal_blocked(client, tmp_path, monkeypatch):
    monkeypatch.setattr(config, "FILE_ROOT", str(tmp_path))
    r = await client.post("/connections", json={"backend": "file", "path": "../escape.db"})
    assert r.status_code == 403


@pytest.mark.asyncio
async def test_file_backend_reopens_persisted_file(client, tmp_path, monkeypatch):
    monkeypatch.setattr(config, "FILE_ROOT", str(tmp_path))
    r = await client.post("/connections", json={"backend": "file", "path": "p.db"})
    cid = r.json()["id"]
    await client.post(f"/connections/{cid}/tables", json={"head_ptr": 0, "table_size": 512, "rec_size": 4})
    await client.post(
        f"/connections/{cid}/tables/0/records",
        json={"payload_b64": base64.b64encode(struct.pack("<i", 7)).decode()},
    )
    await client.delete(f"/connections/{cid}")

    # reconnect to the same file
    r = await client.post("/connections", json={"backend": "file", "path": "p.db"})
    cid2 = r.json()["id"]
    r = await client.get(f"/connections/{cid2}/tables/0")
    assert r.json()["data"]["count"] == 1
