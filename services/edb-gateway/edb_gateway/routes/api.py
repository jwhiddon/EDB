from __future__ import annotations

from fastapi import APIRouter, HTTPException, Query
from fastapi.responses import JSONResponse

from ..connections import connections
from ..models import EdbStatus, PairRequest, RecordPayload, STATUS_HTTP, TableCreateRequest
from ..transports.serial import list_serial_ports

router = APIRouter()


def _status_response(resp_status: EdbStatus, data: dict | None = None) -> JSONResponse:
    code = STATUS_HTTP.get(resp_status, 400)
    body = {"status": resp_status.value}
    if data:
        body["data"] = data
    return JSONResponse(status_code=code, content=body)


@router.get("/devices")
async def list_devices():
    return {"devices": list_serial_ports()}


@router.post("/connections")
async def create_connection(body: dict):
    mock = body.get("port") == "mock" or body.get("mock", False)
    try:
        conn = await connections.create_serial(
            port=body.get("port", "mock"),
            baud=int(body.get("baud", 115200)),
            encrypt=bool(body.get("encrypt", True)),
            pairing_token=body.get("pairing_token"),
            mock=mock,
        )
    except PermissionError as e:
        raise HTTPException(403, str(e)) from e
    except Exception as e:
        raise HTTPException(503, str(e)) from e
    return {"id": conn.id, "encrypt": conn.encrypt}


@router.delete("/connections/{conn_id}")
async def delete_connection(conn_id: str):
    if not connections.get(conn_id):
        raise HTTPException(404, "connection not found")
    await connections.close(conn_id)
    return {"status": "closed"}


@router.post("/connections/{conn_id}/pair")
async def pair_connection(conn_id: str, body: PairRequest):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    from ..transports.encrypted import EncryptedTransport

    if isinstance(conn.transport, EncryptedTransport):
        await conn.transport.pair(body.pairing_token)
        return {"status": "paired"}
    raise HTTPException(400, "connection does not support pairing")


@router.post("/connections/{conn_id}/unlock")
async def unlock_connection(conn_id: str):
    raise HTTPException(501, "unlock is client-side only (browser Web Crypto)")


@router.get("/connections/{conn_id}/tables")
async def list_tables(conn_id: str):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    resp = await conn.backend.info()
    if resp.status != EdbStatus.OK:
        return _status_response(resp.status)
    return _status_response(resp.status, {"tables": resp.data.get("tables", [])})


@router.post("/connections/{conn_id}/tables")
async def create_table(conn_id: str, body: TableCreateRequest):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    rec_size = body.rec_size
    if body.enc_version > 0 and body.plaintext_rec_size:
        rec_size = body.plaintext_rec_size + 16
    resp = await conn.backend.create_table(body.head_ptr, body.table_size, rec_size)
    return _status_response(resp.status, resp.data)


@router.post("/connections/{conn_id}/tables/{head_ptr}/open")
async def open_table(conn_id: str, head_ptr: int):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    resp = await conn.backend.open_table(head_ptr)
    return _status_response(resp.status, resp.data)


@router.get("/connections/{conn_id}/tables/{head_ptr}")
async def table_meta(conn_id: str, head_ptr: int):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    await conn.backend.open_table(head_ptr)
    count_r = await conn.backend.count(head_ptr)
    limit_r = await conn.backend.limit(head_ptr)
    if count_r.status != EdbStatus.OK or limit_r.status != EdbStatus.OK:
        return _status_response(EdbStatus.ERROR)
    return _status_response(
        EdbStatus.OK,
        {
            "head_ptr": head_ptr,
            "count": count_r.data.get("count", 0),
            "limit": limit_r.data.get("limit", 0),
            "enc_version": 0,
            "enc_mode": "e2e_blind",
        },
    )


@router.post("/connections/{conn_id}/tables/{head_ptr}/clear")
async def clear_table(conn_id: str, head_ptr: int):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    resp = await conn.backend.clear(head_ptr)
    return _status_response(resp.status, resp.data)


@router.get("/connections/{conn_id}/tables/{head_ptr}/records")
async def list_records(
    conn_id: str,
    head_ptr: int,
    offset: int = Query(0, ge=0),
    limit: int = Query(50, ge=1, le=500),
):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    await conn.backend.open_table(head_ptr)
    count_r = await conn.backend.count(head_ptr)
    total = int(count_r.data.get("count", 0))
    records = []
    for recno in range(offset + 1, min(total, offset + limit) + 1):
        r = await conn.backend.read_rec(head_ptr, recno)
        if r.status == EdbStatus.OK:
            records.append({"recno": recno, **r.data})
    return _status_response(EdbStatus.OK, {"records": records, "total": total})


@router.get("/connections/{conn_id}/tables/{head_ptr}/records/{recno}")
async def get_record(conn_id: str, head_ptr: int, recno: int):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    resp = await conn.backend.read_rec(head_ptr, recno)
    return _status_response(resp.status, resp.data)


@router.post("/connections/{conn_id}/tables/{head_ptr}/records")
async def append_record(conn_id: str, head_ptr: int, body: RecordPayload):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    resp = await conn.backend.append_rec(head_ptr, body.payload_b64)
    return _status_response(resp.status, resp.data)


@router.put("/connections/{conn_id}/tables/{head_ptr}/records/{recno}")
async def update_record(conn_id: str, head_ptr: int, recno: int, body: RecordPayload):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    resp = await conn.backend.update_rec(head_ptr, recno, body.payload_b64)
    return _status_response(resp.status, resp.data)


@router.delete("/connections/{conn_id}/tables/{head_ptr}/records/{recno}")
async def delete_record(conn_id: str, head_ptr: int, recno: int):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    resp = await conn.backend.delete_rec(head_ptr, recno)
    return _status_response(resp.status, resp.data)


@router.post("/connections/{conn_id}/tables/{head_ptr}/records/{recno}/insert")
async def insert_record(conn_id: str, head_ptr: int, recno: int, body: RecordPayload):
    conn = connections.get(conn_id)
    if not conn:
        raise HTTPException(404, "connection not found")
    resp = await conn.backend.insert_rec(head_ptr, recno, body.payload_b64)
    return _status_response(resp.status, resp.data)
