"""Shared v3 on-disk helpers for host tools (check, vacuum, migrate).

Byte-compatible with EDB.cpp / docs/FORMAT.md.
"""

from __future__ import annotations

import struct
from dataclasses import dataclass
from typing import Callable

FLAG = 0xDB
VERSION = 3
HEADER_COPY_SIZE = 48
HEADER_SPAN = 2 * HEADER_COPY_SIZE
SLOT_LIVE = 0xA5
SLOT_TOMBSTONE = 0x5A
FREE_NONE = 0xFFFFFFFF

HDR_ENCRYPTED = 0x0001
HDR_STABLE_IDS = 0x0002
HDR_RING = 0x0004


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


@dataclass
class CheckIssue:
    severity: str  # "error" | "warn"
    message: str


@dataclass
class RemapEntry:
    record_id: int
    old_recno: int
    new_recno: int


def _parse_header_copy(buf: bytes) -> Header | None:
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


def active_header(data: bytes, head_ptr: int) -> tuple[Header, int] | None:
    off0 = head_ptr
    off1 = head_ptr + HEADER_COPY_SIZE
    h0 = _parse_header_copy(data[off0 : off0 + HEADER_COPY_SIZE])
    h1 = _parse_header_copy(data[off1 : off1 + HEADER_COPY_SIZE])
    if h0 is None and h1 is None:
        if len(data) >= head_ptr + 2 and data[head_ptr] == FLAG and data[head_ptr + 1] != VERSION:
            return None  # legacy
        return None
    if h0 is not None and (h1 is None or h0.seq >= h1.seq):
        return h0, 0
    return h1, 1


def slot_offset(head_ptr: int, h: Header, idx: int) -> int:
    return head_ptr + h.data_offset + idx * h.slot_stride


def read_slot_status(data: bytes, head_ptr: int, h: Header, idx: int) -> int:
    return data[slot_offset(head_ptr, h, idx)]


def read_slot_payload(data: bytes, head_ptr: int, h: Header, idx: int) -> tuple[str, bytes]:
    off = slot_offset(head_ptr, h, idx)
    if off + h.slot_stride > len(data):
        return "truncated", b""
    status = data[off]
    if status == SLOT_TOMBSTONE:
        return "tombstone", b""
    if status != SLOT_LIVE:
        return "empty", b""
    payload = bytes(data[off + 1 : off + 1 + h.rec_size])
    stored = struct.unpack_from("<H", data, off + 1 + h.rec_size)[0]
    if crc16_ccitt_edb(bytes([SLOT_LIVE]) + payload) != stored:
        return "corrupt", payload
    return "live", payload


def record_id_from_payload(payload: bytes) -> int:
    if len(payload) < 4:
        return 0
    return struct.unpack_from("<I", payload, 0)[0]


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
    reserved = bytearray(h.reserved[:14].ljust(14, b"\x00"))
    struct.pack_into("<I", reserved, 0, h.next_record_id)
    buf[30:44] = reserved
    struct.pack_into("<I", buf, 44, crc32_edb(bytes(buf[0:44])))
    return bytes(buf)


def _write_slot(data: bytearray, head_ptr: int, h: Header, idx: int, payload: bytes) -> None:
    off = slot_offset(head_ptr, h, idx)
    need = off + h.slot_stride
    if len(data) < need:
        data.extend(b"\x00" * (need - len(data)))
    crc = crc16_ccitt_edb(bytes([SLOT_LIVE]) + payload)
    data[off + 1 : off + 1 + h.rec_size] = payload[: h.rec_size].ljust(h.rec_size, b"\x00")
    struct.pack_into("<H", data, off + 1 + h.rec_size, crc)
    data[off] = SLOT_LIVE


def _publish(data: bytearray, head_ptr: int, h: Header, active_copy: int) -> int:
    target = 1 - active_copy
    h.seq += 1
    off = head_ptr + target * HEADER_COPY_SIZE
    if len(data) < off + HEADER_COPY_SIZE:
        data.extend(b"\x00" * (off + HEADER_COPY_SIZE - len(data)))
    data[off : off + HEADER_COPY_SIZE] = _pack_header(h)
    return target


def check_table(data: bytes, head_ptr: int = 0) -> tuple[list[CheckIssue], Header | None]:
    issues: list[CheckIssue] = []
    active = active_header(data, head_ptr)
    if active is None:
        if len(data) >= head_ptr + 2 and data[head_ptr] == FLAG and data[head_ptr + 1] < VERSION:
            issues.append(CheckIssue("error", "legacy v1/v2 file — migrate with edb_migrate.py"))
        else:
            issues.append(CheckIssue("error", "no valid v3 header copy"))
        return issues, None

    h, _ = active
    end = head_ptr + h.table_size
    if len(data) < end:
        issues.append(CheckIssue("warn", f"file shorter than table_size ({len(data)} < {end})"))

    live = 0
    tombstones = 0
    seen_ids: set[int] = set()
    for idx in range(h.n_slots):
        kind, payload = read_slot_payload(data, head_ptr, h, idx)
        if kind == "truncated":
            issues.append(CheckIssue("error", f"slot {idx + 1} extends past file end"))
            continue
        if kind == "corrupt":
            issues.append(CheckIssue("error", f"slot {idx + 1} (recno {idx + 1}) CRC mismatch"))
            continue
        if kind == "live":
            live += 1
            if h.stable_ids():
                rid = record_id_from_payload(payload)
                if rid == 0:
                    issues.append(CheckIssue("warn", f"recno {idx + 1} has zero record_id"))
                elif rid in seen_ids:
                    issues.append(CheckIssue("error", f"duplicate record_id {rid}"))
                else:
                    seen_ids.add(rid)
        elif kind == "tombstone":
            tombstones += 1

    if live != h.n_live:
        issues.append(
            CheckIssue(
                "warn",
                f"header n_live={h.n_live} but scanned {live} live slot(s)",
            )
        )

    if tombstones and live + tombstones < h.n_slots:
        issues.append(CheckIssue("warn", "sparse table has never-used slots between live/tombstone"))

    if h.stable_ids() and h.rec_size < 4:
        issues.append(CheckIssue("error", "stable_ids flag set but rec_size < 4"))

    if h.ring_mode():
        if tombstones:
            issues.append(
                CheckIssue("warn", "ring table has tombstone slot(s) — ring mode should be append-only")
            )
        cap = h.max_slots()
        if cap and h.n_live >= cap and h.ring_head >= cap:
            issues.append(
                CheckIssue(
                    "error",
                    f"ring_head={h.ring_head} out of range for full ring (max_slots={cap})",
                )
            )

    return issues, h


def vacuum_table(
    data: bytes,
    head_ptr: int = 0,
    on_remap: Callable[[RemapEntry], None] | None = None,
) -> tuple[bytearray, list[RemapEntry], Header | None]:
    """Repack live slots densely; return new file bytes and recno remap list."""
    active = active_header(data, head_ptr)
    if active is None:
        return bytearray(data), [], None

    h, active_copy = active
    out = bytearray(data)
    if len(out) < head_ptr + h.table_size:
        out.extend(b"\x00" * (head_ptr + h.table_size - len(out)))

    remaps: list[RemapEntry] = []
    dst = 0
    for src in range(h.n_slots):
        kind, payload = read_slot_payload(out, head_ptr, h, src)
        if kind != "live":
            continue
        old_recno = src + 1
        new_recno = dst + 1
        rid = record_id_from_payload(payload) if h.stable_ids() else 0
        if src != dst:
            _write_slot(out, head_ptr, h, dst, payload)
            out[slot_offset(head_ptr, h, src)] = 0
            remaps.append(RemapEntry(rid, old_recno, new_recno))
            if on_remap:
                on_remap(remaps[-1])
        dst += 1

    h.n_slots = dst
    h.n_live = dst
    h.free_head = FREE_NONE
    _publish(out, head_ptr, h, active_copy)
    return out, remaps, h


def table_size_for_slots(h: Header, max_slots: int) -> int:
    """Bytes needed to reserve max_slots (matches EDB_TableSizing / firmware helpers)."""
    if max_slots < 0:
        raise ValueError("max_slots must be non-negative")
    return h.data_offset + max_slots * h.slot_stride


def scan_v3_table_offsets(data: bytes, start: int = 0) -> list[int]:
    """Return head_ptr offsets of contiguous v3 tables from start through the file."""
    offsets: list[int] = []
    pos = start
    while pos + HEADER_COPY_SIZE <= len(data):
        active = active_header(data, pos)
        if active is None:
            break
        h, _ = active
        offsets.append(pos)
        if h.table_size == 0:
            break
        pos += h.table_size
    return offsets


@dataclass
class GrowResult:
    data: bytearray
    header: Header
    old_table_size: int
    new_table_size: int
    shifted_tables: list[tuple[int, int]]  # (old_head_ptr, new_head_ptr)


def grow_table(
    data: bytes,
    head_ptr: int = 0,
    *,
    new_table_size: int | None = None,
    add_slots: int | None = None,
    limit_slots: int | None = None,
) -> GrowResult:
    """Increase table_size; shift bytes after the table if the file continues."""
    active = active_header(data, head_ptr)
    if active is None:
        raise ValueError("no valid v3 header at offset")

    h, active_copy = active
    old_table_size = h.table_size

    if sum(x is not None for x in (new_table_size, add_slots, limit_slots)) != 1:
        raise ValueError("specify exactly one of new_table_size, add_slots, or limit_slots")

    if limit_slots is not None:
        if limit_slots < h.n_slots:
            raise ValueError(
                f"limit_slots={limit_slots} is smaller than allocated n_slots={h.n_slots}"
            )
        new_table_size = table_size_for_slots(h, limit_slots)
    elif add_slots is not None:
        if add_slots <= 0:
            raise ValueError("add_slots must be positive")
        new_table_size = table_size_for_slots(h, h.max_slots() + add_slots)
    assert new_table_size is not None

    if new_table_size <= old_table_size:
        raise ValueError(
            f"new table_size ({new_table_size}) must be greater than current ({old_table_size})"
        )
    min_size = table_size_for_slots(h, h.n_slots)
    if new_table_size < min_size:
        raise ValueError(
            f"new table_size ({new_table_size}) cannot fit existing n_slots={h.n_slots}"
        )
    if new_table_size > 0xFFFFFFFF:
        raise ValueError("table_size exceeds uint32")

    delta = new_table_size - old_table_size
    old_end = head_ptr + old_table_size

    tables_after = [off for off in scan_v3_table_offsets(data, old_end) if off >= old_end]
    shifted = [(off, off + delta) for off in tables_after]

    out = bytearray(data)
    if len(out) < old_end:
        out.extend(b"\x00" * (old_end - len(out)))

    tail = out[old_end:]
    out = out[:old_end] + bytearray(delta) + tail

    # New capacity is zero-filled; existing slots are unchanged at the same offsets.
    h.table_size = new_table_size
    _publish(out, head_ptr, h, active_copy)

    return GrowResult(out, h, old_table_size, new_table_size, shifted)
