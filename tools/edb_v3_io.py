"""Offline whole-file v3 operations for host tools (check, vacuum, grow).

The low-level format primitives (constants, CRCs, Header, header pack/parse, slot framing) live in
[edb_v3_format.py](edb_v3_format.py) — the single Python source of truth, shared with the gateway.
This module keeps only the higher-level operations and re-exports the primitives so existing
`from edb_v3_io import ...` callers keep working. Byte-compatible with EDB.cpp / docs/FORMAT.md.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Callable

from edb_v3_format import (
    FREE_NONE,
    HDR_ENCRYPTED,  # noqa: F401  (re-exported for callers)
    HDR_RING,
    HDR_STABLE_IDS,
    HEADER_COPY_SIZE,
    HEADER_SPAN,
    FLAG,
    SLOT_LIVE,
    SLOT_TOMBSTONE,
    VERSION,
    Header,
    crc16_ccitt_edb,  # noqa: F401  (re-exported)
    crc32_edb,  # noqa: F401  (re-exported)
    pack_header,
    publish,
    read_slot_raw,
    record_id_from_payload,
    select_active,
    slot_offset,
    write_slot,
)

# Backward-compatible private aliases kept for existing tools/tests importing them from edb_v3_io.
_pack_header = pack_header
_write_slot = write_slot
_publish = publish


@dataclass
class CheckIssue:
    severity: str  # "error" | "warn"
    message: str


@dataclass
class RemapEntry:
    record_id: int
    old_recno: int
    new_recno: int


def active_header(data: bytes, head_ptr: int) -> tuple[Header, int] | None:
    """Return (header, active_copy) for the newest valid header copy, or None."""
    h, copy, _is_legacy = select_active(data, head_ptr)
    return None if h is None else (h, copy)


def read_slot_status(data: bytes, head_ptr: int, h: Header, idx: int) -> int:
    return data[slot_offset(head_ptr, h, idx)]


def read_slot_payload(data: bytes, head_ptr: int, h: Header, idx: int) -> tuple[str, bytes]:
    """Map a slot to this module's status vocabulary:
    'truncated' | 'tombstone' | 'empty' | 'corrupt' | 'live'."""
    res = read_slot_raw(data, head_ptr, h, idx)
    if res is None:
        return "truncated", b""
    status, payload, crc_ok = res
    if status == SLOT_TOMBSTONE:
        return "tombstone", b""
    if status != SLOT_LIVE:
        return "empty", b""
    if not crc_ok:
        return "corrupt", payload
    return "live", payload


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
