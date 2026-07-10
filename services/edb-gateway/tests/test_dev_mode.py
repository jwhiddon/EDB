import pytest
from httpx import ASGITransport, AsyncClient


@pytest.mark.asyncio
async def test_plaintext_blocked_without_insecure(monkeypatch):
    monkeypatch.delenv("EDB_GATEWAY_INSECURE", raising=False)
    from edb_gateway.connections import ConnectionManager

    mgr = ConnectionManager()
    with pytest.raises(PermissionError):
        await mgr.create_serial(port="mock", encrypt=False, mock=True)


@pytest.mark.asyncio
async def test_plaintext_allowed_with_insecure(monkeypatch):
    monkeypatch.setenv("EDB_GATEWAY_INSECURE", "1")
    from edb_gateway.connections import ConnectionManager

    mgr = ConnectionManager()
    conn = await mgr.create_serial(port="mock", encrypt=False, mock=True)
    assert conn.id
    await mgr.close(conn.id)
