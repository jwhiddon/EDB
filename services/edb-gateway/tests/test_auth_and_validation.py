import pytest
from httpx import ASGITransport, AsyncClient

from edb_gateway import config
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
async def test_api_key_enforced_when_configured(client, monkeypatch):
    # require_api_key reads config.API_KEY at call time, so patching it takes effect live.
    monkeypatch.setattr(config, "API_KEY", "s3cr3t-key")
    r = await client.get("/devices")
    assert r.status_code == 401
    r_ok = await client.get("/devices", headers={"X-API-Key": "s3cr3t-key"})
    assert r_ok.status_code == 200
    r_bad = await client.get("/devices", headers={"X-API-Key": "wrong"})
    assert r_bad.status_code == 401


@pytest.mark.asyncio
async def test_no_key_allows_loopback_dev(client):
    # Default config has no API key -> loopback dev mode allows requests.
    assert config.API_KEY is None
    r = await client.get("/devices")
    assert r.status_code == 200


@pytest.mark.asyncio
async def test_create_table_rejects_invalid_sizes(client, conn_id):
    r_zero_table = await client.post(
        f"/connections/{conn_id}/tables",
        json={"head_ptr": 0, "table_size": 0, "rec_size": 8},
    )
    assert r_zero_table.status_code == 422

    r_zero_rec = await client.post(
        f"/connections/{conn_id}/tables",
        json={"head_ptr": 0, "table_size": 8192, "rec_size": 0},
    )
    assert r_zero_rec.status_code == 422

    r_neg_head = await client.post(
        f"/connections/{conn_id}/tables",
        json={"head_ptr": -1, "table_size": 8192, "rec_size": 8},
    )
    assert r_neg_head.status_code == 422


@pytest.mark.asyncio
async def test_record_payload_length_bounded(client, conn_id):
    huge = "A" * 200000  # far beyond MAX_PAYLOAD_B64
    r = await client.post(
        f"/connections/{conn_id}/tables/0/records",
        json={"payload_b64": huge},
    )
    assert r.status_code == 422


@pytest.mark.asyncio
async def test_cors_rejects_disallowed_origin(client):
    # Preflight from a hostile origin is rejected outright.
    r = await client.options(
        "/devices",
        headers={"Origin": "http://evil.example", "Access-Control-Request-Method": "GET"},
    )
    assert r.status_code == 400
    # A simple request from a disallowed origin gets no allow-origin header (the browser blocks it).
    r2 = await client.get("/devices", headers={"Origin": "http://evil.example"})
    assert "access-control-allow-origin" not in {k.lower() for k in r2.headers}
    # An allowlisted origin is echoed back.
    r3 = await client.get("/devices", headers={"Origin": "http://127.0.0.1:8765"})
    assert r3.headers.get("access-control-allow-origin") == "http://127.0.0.1:8765"


@pytest.mark.asyncio
async def test_trusted_host_rejects_bad_host(client):
    # DNS-rebinding defense: a request whose Host header is not allowlisted is refused.
    r = await client.get("/devices", headers={"Host": "evil.example"})
    assert r.status_code == 400
