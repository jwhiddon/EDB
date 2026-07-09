"""Host-side reader/writer for the EDB v3 on-disk format.

Byte-compatible with EDB.cpp so a file written here opens on the device and vice versa:
redundant CRC32 dual header, framed slots (status + CRC16), tombstone delete, intrusive
free-list. See docs/FORMAT.md.
"""

from __future__ import annotations

import os
import struct
from dataclasses import dataclass

FLAG = 0xDB
VERSION = 3
HEADER_COPY_SIZE = 48
HEADER_SPAN = 2 * HEADER_COPY_SIZE
SLOT_LIVE = 0xA5
SLOT_TOMBSTONE = 0x5A
FREE_NONE = 0xFFFFFFFF

# Status strings (match EdbStatus values).
OK = "EDB_OK"
ERROR = "EDB_ERROR"
OUT_OF_RANGE = "EDB_OUT_OF_RANGE"
TABLE_FULL = "EDB_TABLE_FULL"
DELETED = "EDB_DELETED"
CORRUPT = "EDB_CORRUPT"
NEEDS_MIGRATION = "EDB_NEEDS_MIGRATION"


def crc32_edb(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if (crc & 1) else (crc >> 1)
    return crc ^ 0xFFFFFFFF


def crc16_ccitt_edb(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        crc &= 0xFFFF
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


@dataclass
class Header:
    flags: int
    seq: int
    n_slots: int
    n_live: int
    rec_size: int
    slot_stride: int
    table_size: int
    free_head: int
    data_offset: int

    def max_slots(self) -> int:
        if self.slot_stride == 0 or self.table_size < self.data_offset:
            return 0
        return (self.table_size - self.data_offset) // self.slot_stride


def _pack_header(h: Header) -> bytes:
    buf = bytearray(HEADER_COPY_SIZE)
    buf[0] = FLAG
    buf[1] = VERSION
    struct.pack_into("<H", buf, 2, h.flags)
    struct.pack_into("<I", buf, 4, h.seq)
    struct.pack_into("<I", buf, 8, h.n_slots)
    struct.pack_into("<I", buf, 12, h.n_live)
    struct.pack_into("<H", buf, 16, h.rec_size)
    struct.pack_into("<H", buf, 18, h.slot_stride)
    struct.pack_into("<I", buf, 20, h.table_size)
    struct.pack_into("<I", buf, 24, h.free_head)
    struct.pack_into("<H", buf, 28, h.data_offset)
    struct.pack_into("<I", buf, 44, crc32_edb(bytes(buf[0:44])))
    return bytes(buf)


def _parse_header(buf: bytes) -> Header | None:
    if len(buf) < HEADER_COPY_SIZE or buf[0] != FLAG or buf[1] != VERSION:
        return None
    if struct.unpack_from("<I", buf, 44)[0] != crc32_edb(bytes(buf[0:44])):
        return None
    h = Header(
        flags=struct.unpack_from("<H", buf, 2)[0],
        seq=struct.unpack_from("<I", buf, 4)[0],
        n_slots=struct.unpack_from("<I", buf, 8)[0],
        n_live=struct.unpack_from("<I", buf, 12)[0],
        rec_size=struct.unpack_from("<H", buf, 16)[0],
        slot_stride=struct.unpack_from("<H", buf, 18)[0],
        table_size=struct.unpack_from("<I", buf, 20)[0],
        free_head=struct.unpack_from("<I", buf, 24)[0],
        data_offset=struct.unpack_from("<H", buf, 28)[0],
    )
    if h.rec_size == 0 or h.slot_stride < 1 + h.rec_size + 2:
        return None
    if h.data_offset < HEADER_COPY_SIZE or h.table_size < h.data_offset + h.slot_stride:
        return None
    if h.n_slots > h.max_slots() or h.n_live > h.n_slots:
        return None
    if h.free_head != FREE_NONE and h.free_head >= h.n_slots:
        return None
    return h


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

    # ---- headers ----
    def _read_copy(self, head_ptr: int, copy: int) -> Header | None:
        off = head_ptr + copy * HEADER_COPY_SIZE
        return _parse_header(bytes(self.data[off : off + HEADER_COPY_SIZE]))

    def _active(self, head_ptr: int) -> tuple[Header, int]:
        """Return (header, active_copy). Raises OpenError with a status on failure."""
        h0 = self._read_copy(head_ptr, 0)
        h1 = self._read_copy(head_ptr, 1)
        if h0 is None and h1 is None:
            if len(self.data) >= head_ptr + 2 and self.data[head_ptr] == FLAG and self.data[head_ptr + 1] != VERSION:
                raise OpenError(NEEDS_MIGRATION)
            raise OpenError(ERROR)
        if h0 is not None and (h1 is None or h0.seq >= h1.seq):
            return h0, 0
        return h1, 1

    def _publish(self, head_ptr: int, header: Header, active_copy: int) -> None:
        target = 1 - active_copy
        header.seq += 1
        off = head_ptr + target * HEADER_COPY_SIZE
        self._ensure(off + HEADER_COPY_SIZE)
        self.data[off : off + HEADER_COPY_SIZE] = _pack_header(header)

    # ---- slots ----
    def _slot_off(self, head_ptr: int, h: Header, idx: int) -> int:
        return head_ptr + h.data_offset + idx * h.slot_stride

    def _read_slot(self, head_ptr: int, h: Header, idx: int) -> tuple[str, bytes]:
        off = self._slot_off(head_ptr, h, idx)
        status = self.data[off]
        if status == SLOT_TOMBSTONE:
            return DELETED, b""
        if status != SLOT_LIVE:
            return OUT_OF_RANGE, b""
        payload = bytes(self.data[off + 1 : off + 1 + h.rec_size])
        stored = struct.unpack_from("<H", self.data, off + 1 + h.rec_size)[0]
        if crc16_ccitt_edb(bytes([SLOT_LIVE]) + payload) != stored:
            return CORRUPT, b""
        return OK, payload

    def _write_slot(self, head_ptr: int, h: Header, idx: int, payload: bytes) -> None:
        off = self._slot_off(head_ptr, h, idx)
        self._ensure(off + h.slot_stride)
        crc = crc16_ccitt_edb(bytes([SLOT_LIVE]) + payload)
        self.data[off + 1 : off + 1 + h.rec_size] = payload
        struct.pack_into("<H", self.data, off + 1 + h.rec_size, crc)
        self.data[off] = SLOT_LIVE

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
        packed = _pack_header(h)
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
