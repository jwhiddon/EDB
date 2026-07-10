"""Host-side reader/writer for the EDB v3 on-disk format (the gateway's live file handle).

The format primitives (constants, CRCs, Header, header pack/parse, slot framing) come from the
shared, single-source module `tools/edb_v3_format.py`, which mirrors EDB.cpp. This file adds the
gateway's `EdbV3File` handle and status-string vocabulary on top. A file written here opens on the
device and vice versa. See docs/FORMAT.md.
"""

from __future__ import annotations

import os
import struct
import sys
from pathlib import Path

try:
    from edb_v3_format import (
        FREE_NONE,
        HEADER_COPY_SIZE,
        HEADER_SPAN,
        SLOT_LIVE,
        SLOT_TOMBSTONE,
        Header,
        crc16_ccitt_edb,  # noqa: F401  (re-exported for callers/tests)
        crc32_edb,  # noqa: F401  (re-exported for callers/tests)
        pack_header,
        publish,
        read_slot_raw,
        select_active,
        slot_offset,
        write_slot,
    )
except ImportError:
    # The shared module lives in the repo's tools/ dir. The gateway runs from within the repo
    # (editable install / tests), so locate it relative to this file: services/edb-gateway/
    # edb_gateway/edb_v3.py -> parents[3] is the repo root.
    _TOOLS = Path(__file__).resolve().parents[3] / "tools"
    if str(_TOOLS) not in sys.path:
        sys.path.insert(0, str(_TOOLS))
    from edb_v3_format import (  # noqa: E402
        FREE_NONE,
        HEADER_COPY_SIZE,
        HEADER_SPAN,
        SLOT_LIVE,
        SLOT_TOMBSTONE,
        Header,
        crc16_ccitt_edb,  # noqa: F401
        crc32_edb,  # noqa: F401
        pack_header,
        publish,
        read_slot_raw,
        select_active,
        slot_offset,
        write_slot,
    )

# Status strings (match EdbStatus values). Gateway-specific REST vocabulary.
OK = "EDB_OK"
ERROR = "EDB_ERROR"
OUT_OF_RANGE = "EDB_OUT_OF_RANGE"
TABLE_FULL = "EDB_TABLE_FULL"
DELETED = "EDB_DELETED"
CORRUPT = "EDB_CORRUPT"
NEEDS_MIGRATION = "EDB_NEEDS_MIGRATION"


class OpenError(Exception):
    def __init__(self, status: str) -> None:
        super().__init__(status)
        self.status = status


class EdbV3File:
    """One or more v3 tables inside a single file, addressed by head_ptr."""

    def __init__(self, path: str) -> None:
        self.path = path
        self.data = bytearray()
        if os.path.exists(path):
            with open(path, "rb") as fh:
                self.data = bytearray(fh.read())

    # ---- persistence ----
    def _ensure(self, size: int) -> None:
        if len(self.data) < size:
            self.data.extend(b"\x00" * (size - len(self.data)))

    def _flush(self) -> None:
        tmp = self.path + ".tmp"
        with open(tmp, "wb") as fh:
            fh.write(self.data)
        os.replace(tmp, self.path)

    # ---- headers (delegate to the shared format module) ----
    def _active(self, head_ptr: int) -> tuple[Header, int]:
        """Return (header, active_copy). Raises OpenError with a status on failure."""
        h, copy, is_legacy = select_active(self.data, head_ptr)
        if h is None:
            raise OpenError(NEEDS_MIGRATION if is_legacy else ERROR)
        return h, copy

    def _publish(self, head_ptr: int, header: Header, active_copy: int) -> None:
        publish(self.data, head_ptr, header, active_copy)

    # ---- slots (delegate to the shared format module) ----
    def _slot_off(self, head_ptr: int, h: Header, idx: int) -> int:
        return slot_offset(head_ptr, h, idx)

    def _read_slot(self, head_ptr: int, h: Header, idx: int) -> tuple[str, bytes]:
        res = read_slot_raw(self.data, head_ptr, h, idx)
        if res is None:
            return OUT_OF_RANGE, b""
        status, payload, crc_ok = res
        if status == SLOT_TOMBSTONE:
            return DELETED, b""
        if status != SLOT_LIVE:
            return OUT_OF_RANGE, b""
        if not crc_ok:
            return CORRUPT, b""
        return OK, payload

    def _write_slot(self, head_ptr: int, h: Header, idx: int, payload: bytes) -> None:
        write_slot(self.data, head_ptr, h, idx, payload)

    def _has_free_list(self, h: Header) -> bool:
        return h.rec_size >= 4

    def _alloc_slot(self, head_ptr: int, h: Header) -> int | None:
        if self._has_free_list(h) and h.free_head != FREE_NONE:
            idx = h.free_head
            off = self._slot_off(head_ptr, h, idx)
            if idx < h.n_slots and self.data[off] == SLOT_TOMBSTONE:
                nxt = struct.unpack_from("<I", self.data, off + 1)[0]
                h.free_head = nxt if (nxt == FREE_NONE or nxt < h.n_slots) else FREE_NONE
                return idx
            h.free_head = FREE_NONE
        if h.n_slots < h.max_slots():
            idx = h.n_slots
            h.n_slots += 1
            return idx
        for i in range(h.n_slots):
            if self.data[self._slot_off(head_ptr, h, i)] == SLOT_TOMBSTONE:
                return i
        return None

    # ---- public operations ----
    def create(self, head_ptr: int, table_size: int, rec_size: int) -> str:
        if rec_size <= 0 or rec_size > 0xFFFF - 3 or table_size > 0xFFFFFFFF:
            return ERROR
        stride = 1 + rec_size + 2
        if table_size < HEADER_SPAN + stride:
            return ERROR
        h = Header(0, 1, 0, 0, rec_size, stride, table_size, FREE_NONE, HEADER_SPAN)
        self._ensure(head_ptr + table_size)
        packed = pack_header(h)
        self.data[head_ptr : head_ptr + HEADER_COPY_SIZE] = packed
        self.data[head_ptr + HEADER_COPY_SIZE : head_ptr + HEADER_SPAN] = packed
        self._flush()
        return OK

    def open(self, head_ptr: int) -> str:
        try:
            self._active(head_ptr)
            return OK
        except OpenError as exc:
            return exc.status

    def count(self, head_ptr: int) -> int:
        return self._active(head_ptr)[0].n_live

    def limit(self, head_ptr: int) -> int:
        return self._active(head_ptr)[0].max_slots()

    def rec_size(self, head_ptr: int) -> int:
        return self._active(head_ptr)[0].rec_size

    def clear(self, head_ptr: int) -> str:
        h, active = self._active(head_ptr)
        h.n_slots = 0
        h.n_live = 0
        h.free_head = FREE_NONE
        self._publish(head_ptr, h, active)
        self._flush()
        return OK

    def read_rec(self, head_ptr: int, recno: int) -> tuple[str, bytes]:
        h, _ = self._active(head_ptr)
        if recno < 1 or recno > h.n_slots:
            return OUT_OF_RANGE, b""
        return self._read_slot(head_ptr, h, recno - 1)

    def append_rec(self, head_ptr: int, payload: bytes) -> tuple[str, int]:
        h, active = self._active(head_ptr)
        payload = self._fit(payload, h.rec_size)
        idx = self._alloc_slot(head_ptr, h)
        if idx is None:
            return TABLE_FULL, 0
        self._write_slot(head_ptr, h, idx, payload)
        h.n_live += 1
        self._publish(head_ptr, h, active)
        self._flush()
        return OK, idx + 1

    def update_rec(self, head_ptr: int, recno: int, payload: bytes) -> str:
        h, _ = self._active(head_ptr)
        if recno < 1 or recno > h.n_slots:
            return OUT_OF_RANGE
        off = self._slot_off(head_ptr, h, recno - 1)
        if self.data[off] == SLOT_TOMBSTONE:
            return DELETED
        if self.data[off] != SLOT_LIVE:
            return OUT_OF_RANGE
        self._write_slot(head_ptr, h, recno - 1, self._fit(payload, h.rec_size))
        self._flush()
        return OK

    def delete_rec(self, head_ptr: int, recno: int) -> str:
        h, active = self._active(head_ptr)
        if recno < 1 or recno > h.n_slots:
            return OUT_OF_RANGE
        idx = recno - 1
        off = self._slot_off(head_ptr, h, idx)
        if self.data[off] == SLOT_TOMBSTONE:
            return DELETED
        if self.data[off] != SLOT_LIVE:
            return OUT_OF_RANGE
        if self._has_free_list(h):
            struct.pack_into("<I", self.data, off + 1, h.free_head)
            h.free_head = idx
        self.data[off] = SLOT_TOMBSTONE
        h.n_live -= 1
        self._publish(head_ptr, h, active)
        self._flush()
        return OK

    def insert_rec(self, head_ptr: int, recno: int, payload: bytes) -> tuple[str, int]:
        # v3: positional order is not preserved; allocate a free slot like append.
        return self.append_rec(head_ptr, payload)

    @staticmethod
    def _fit(payload: bytes, rec_size: int) -> bytes:
        if len(payload) > rec_size:
            return payload[:rec_size]
        return payload + b"\x00" * (rec_size - len(payload))
