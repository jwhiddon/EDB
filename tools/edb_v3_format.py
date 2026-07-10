#!/usr/bin/env python3
"""Single source of truth for the EDB **v3 on-disk format** on the Python side.

Both host consumers import this module instead of re-implementing the format:
  - tools/edb_v3_io.py                        (offline check / vacuum / grow)
  - services/edb-gateway/edb_gateway/edb_v3.py (live file handle for the gateway)

Everything here mirrors the C++ device implementation in EDB.cpp / EDB.h, which remains the ultimate
source of truth. The constants, CRC polynomials, header field offsets, and slot framing MUST match
it byte-for-byte; the cross-language parity tests (tools/test_datasets.py,
services/edb-gateway/tests/test_edb_v3.py, and the native suite loading Python-migrated fixtures)
enforce that agreement. Change the format in EDB.cpp and here together.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass

# ---- format constants (match EDB.h) -------------------------------------------------------------
FLAG = 0xDB
VERSION = 3
HEADER_COPY_SIZE = 48
HEADER_SPAN = 2 * HEADER_COPY_SIZE  # default data_offset (two header copies)
SLOT_LIVE = 0xA5
SLOT_TOMBSTONE = 0x5A
FREE_NONE = 0xFFFFFFFF

# header flag bits
HDR_ENCRYPTED = 0x0001
HDR_STABLE_IDS = 0x0002
HDR_RING = 0x0004
HDR_BATCH = 0x0008


# ---- checksums (match EDB.cpp) ------------------------------------------------------------------
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


# ---- header -------------------------------------------------------------------------------------
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
    next_record_id: int = 1
    ring_head: int = 0
    reserved: bytes = b"\x00" * 14

    def max_slots(self) -> int:
        if self.slot_stride == 0 or self.table_size < self.data_offset:
            return 0
        return (self.table_size - self.data_offset) // self.slot_stride

    def stable_ids(self) -> bool:
        return bool(self.flags & HDR_STABLE_IDS)

    def ring_mode(self) -> bool:
        return bool(self.flags & HDR_RING)


def pack_header(h: Header) -> bytes:
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
    # reserved[0..3] = next_record_id, reserved[4..7] = ring_head (LE32); rest zero
    reserved = bytearray(h.reserved[:14].ljust(14, b"\x00"))
    struct.pack_into("<I", reserved, 0, h.next_record_id)
    struct.pack_into("<I", reserved, 4, h.ring_head)
    buf[30:44] = reserved
    struct.pack_into("<I", buf, 44, crc32_edb(bytes(buf[0:44])))
    return bytes(buf)


def parse_header_copy(buf: bytes) -> Header | None:
    """Parse and validate one 48-byte header copy. Returns None if magic/version/CRC or an
    invariant fails."""
    if len(buf) < HEADER_COPY_SIZE or buf[0] != FLAG or buf[1] != VERSION:
        return None
    if struct.unpack_from("<I", buf, 44)[0] != crc32_edb(bytes(buf[0:44])):
        return None
    reserved = bytes(buf[30:44])
    next_record_id = struct.unpack_from("<I", reserved, 0)[0] if len(reserved) >= 4 else 1
    ring_head = struct.unpack_from("<I", reserved, 4)[0] if len(reserved) >= 8 else 0
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
        next_record_id=next_record_id,
        ring_head=ring_head,
        reserved=reserved,
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


def select_active(data: bytes, head_ptr: int) -> tuple[Header | None, int, bool]:
    """Pick the valid header copy with the highest seq (ping-pong publish).

    Returns (header, active_copy, is_legacy):
      - (Header, 0|1, False) when a copy validates;
      - (None, -1, True) when neither validates but a legacy v1/v2 magic is present;
      - (None, -1, False) when the region is not a recognizable EDB header.
    """
    h0 = parse_header_copy(bytes(data[head_ptr : head_ptr + HEADER_COPY_SIZE]))
    h1 = parse_header_copy(bytes(data[head_ptr + HEADER_COPY_SIZE : head_ptr + 2 * HEADER_COPY_SIZE]))
    if h0 is None and h1 is None:
        is_legacy = (len(data) >= head_ptr + 2
                     and data[head_ptr] == FLAG and data[head_ptr + 1] != VERSION)
        return None, -1, is_legacy
    if h0 is not None and (h1 is None or h0.seq >= h1.seq):
        return h0, 0, False
    return h1, 1, False


def publish(data: bytearray, head_ptr: int, h: Header, active_copy: int) -> int:
    """Write the header to the *other* copy with seq+1 (crash-safe publish). Grows `data` if needed.
    Returns the copy index written."""
    target = 1 - active_copy
    h.seq += 1
    off = head_ptr + target * HEADER_COPY_SIZE
    if len(data) < off + HEADER_COPY_SIZE:
        data.extend(b"\x00" * (off + HEADER_COPY_SIZE - len(data)))
    data[off : off + HEADER_COPY_SIZE] = pack_header(h)
    return target


# ---- slots --------------------------------------------------------------------------------------
def slot_offset(head_ptr: int, h: Header, idx: int) -> int:
    return head_ptr + h.data_offset + idx * h.slot_stride


def slot_crc(payload: bytes) -> int:
    return crc16_ccitt_edb(bytes([SLOT_LIVE]) + payload)


def read_slot_raw(data: bytes, head_ptr: int, h: Header,
                  idx: int) -> tuple[int, bytes, bool] | None:
    """Low-level slot read. Returns (status_byte, payload, crc_ok), or None if the slot is truncated
    (extends past the data). `payload`/`crc_ok` are only meaningful when status_byte == SLOT_LIVE.
    Callers map the result to their own status vocabulary."""
    off = slot_offset(head_ptr, h, idx)
    if off + h.slot_stride > len(data):
        return None
    status = data[off]
    if status != SLOT_LIVE:
        return status, b"", False
    payload = bytes(data[off + 1 : off + 1 + h.rec_size])
    stored = struct.unpack_from("<H", data, off + 1 + h.rec_size)[0]
    return status, payload, (slot_crc(payload) == stored)


def write_slot(data: bytearray, head_ptr: int, h: Header, idx: int, payload: bytes) -> None:
    """Write a LIVE slot (payload padded/truncated to rec_size, CRC, then status). Grows `data`."""
    off = slot_offset(head_ptr, h, idx)
    need = off + h.slot_stride
    if len(data) < need:
        data.extend(b"\x00" * (need - len(data)))
    payload = payload[: h.rec_size].ljust(h.rec_size, b"\x00")
    data[off + 1 : off + 1 + h.rec_size] = payload
    struct.pack_into("<H", data, off + 1 + h.rec_size, slot_crc(payload))
    data[off] = SLOT_LIVE


def record_id_from_payload(payload: bytes) -> int:
    if len(payload) < 4:
        return 0
    return struct.unpack_from("<I", payload, 0)[0]
