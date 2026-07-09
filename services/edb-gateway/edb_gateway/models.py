from __future__ import annotations

from enum import Enum
from typing import Any

from pydantic import BaseModel, Field


class EdbStatus(str, Enum):
    OK = "EDB_OK"
    ERROR = "EDB_ERROR"
    OUT_OF_RANGE = "EDB_OUT_OF_RANGE"
    TABLE_FULL = "EDB_TABLE_FULL"


STATUS_HTTP = {
    EdbStatus.OK: 200,
    EdbStatus.ERROR: 400,
    EdbStatus.OUT_OF_RANGE: 422,
    EdbStatus.TABLE_FULL: 409,
}


class SerialConnectionRequest(BaseModel):
    transport: str = "serial"
    port: str
    baud: int = 115200
    encrypt: bool = True
    pairing_token: str | None = None


class TableCreateRequest(BaseModel):
    head_ptr: int
    table_size: int
    rec_size: int
    enc_version: int = 0
    enc_mode: str = "e2e_blind"
    plaintext_rec_size: int | None = None


class RecordPayload(BaseModel):
    payload_b64: str


class PairRequest(BaseModel):
    pairing_token: str


class ProtocolMessage(BaseModel):
    id: int
    cmd: str
    head_ptr: int | None = None
    recno: int | None = None
    table_size: int | None = None
    rec_size: int | None = None
    payload_b64: str | None = None
    token: str | None = None

    def to_wire(self) -> dict[str, Any]:
        d: dict[str, Any] = {"id": self.id, "cmd": self.cmd}
        for k in ("head_ptr", "recno", "table_size", "rec_size", "payload_b64", "token"):
            v = getattr(self, k)
            if v is not None:
                d[k] = v
        return d


class ProtocolResponse(BaseModel):
    id: int
    status: EdbStatus
    data: dict[str, Any] = Field(default_factory=dict)

    @classmethod
    def from_dict(cls, d: dict[str, Any]) -> ProtocolResponse:
        return cls(
            id=int(d["id"]),
            status=EdbStatus(d.get("status", "EDB_ERROR")),
            data=d.get("data") or {},
        )
