from __future__ import annotations

from enum import Enum
from typing import Any

from pydantic import BaseModel, Field


class EdbStatus(str, Enum):
    OK = "EDB_OK"
    ERROR = "EDB_ERROR"
    OUT_OF_RANGE = "EDB_OUT_OF_RANGE"
    TABLE_FULL = "EDB_TABLE_FULL"
    DELETED = "EDB_DELETED"
    CORRUPT = "EDB_CORRUPT"
    NEEDS_MIGRATION = "EDB_NEEDS_MIGRATION"


STATUS_HTTP = {
    EdbStatus.OK: 200,
    EdbStatus.ERROR: 400,
    EdbStatus.OUT_OF_RANGE: 422,
    EdbStatus.TABLE_FULL: 409,
    EdbStatus.DELETED: 404,
    EdbStatus.CORRUPT: 422,
    EdbStatus.NEEDS_MIGRATION: 409,
}

# Conservative upper bounds; the device enforces its own limits too.
MAX_TABLE_SIZE = 16 * 1024 * 1024
MAX_REC_SIZE = 65535
MAX_PAYLOAD_B64 = 4 * ((MAX_REC_SIZE + 2) // 3) + 8


class SerialConnectionRequest(BaseModel):
    transport: str = "serial"
    port: str
    baud: int = 115200
    encrypt: bool = True
    pairing_token: str | None = None


class TableCreateRequest(BaseModel):
    head_ptr: int = Field(ge=0)
    table_size: int = Field(gt=0, le=MAX_TABLE_SIZE)
    rec_size: int = Field(gt=0, le=MAX_REC_SIZE)
    enc_version: int = Field(default=0, ge=0)
    enc_mode: str = "e2e_blind"
    plaintext_rec_size: int | None = Field(default=None, gt=0, le=MAX_REC_SIZE)


class RecordPayload(BaseModel):
    payload_b64: str = Field(min_length=1, max_length=MAX_PAYLOAD_B64)


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
