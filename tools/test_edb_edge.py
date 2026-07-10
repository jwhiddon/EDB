"""Edge-case tests for large v3 files and host tool scalability."""

from __future__ import annotations

import struct
import unittest

from edb_v3_io import (
    HEADER_SPAN,
    HDR_RING,
    HDR_STABLE_IDS,
    SLOT_TOMBSTONE,
    Header,
    _pack_header,
    _write_slot,
    active_header,
    check_table,
    grow_table,
    scan_v3_table_offsets,
    slot_offset,
    table_size_for_slots,
    vacuum_table,
)

# Keep CI fast; raise for local stress runs.
LARGE_SLOT_COUNT = 20_000
MEGABYTE = 1024 * 1024


def _publish_header(data: bytearray, head_ptr: int, h: Header) -> None:
    packed = _pack_header(h)
    data[head_ptr : head_ptr + HEADER_SPAN] = packed + packed


def _sparse_table(
    n_slots: int,
    *,
    rec_size: int = 4,
    tombstone_odd: bool = True,
    stable_ids: bool = False,
) -> bytearray:
    stride = 1 + rec_size + 2
    table_size = table_size_for_slots(
        Header(0, 1, 0, 0, rec_size, stride, 0, 0xFFFFFFFF, HEADER_SPAN),
        n_slots,
    )
    flags = HDR_STABLE_IDS if stable_ids else 0
    live = 0
    for idx in range(n_slots):
        if tombstone_odd and (idx % 2 == 1):
            continue
        live += 1
    h = Header(
        flags=flags,
        seq=1,
        n_slots=n_slots,
        n_live=live,
        rec_size=rec_size,
        slot_stride=stride,
        table_size=table_size,
        free_head=0xFFFFFFFF,
        data_offset=HEADER_SPAN,
        next_record_id=live + 1 if stable_ids else 1,
    )
    data = bytearray(table_size)
    _publish_header(data, 0, h)
    for idx in range(n_slots):
        if tombstone_odd and (idx % 2 == 1):
            data[slot_offset(0, h, idx)] = SLOT_TOMBSTONE
            continue
        if stable_ids:
            payload = struct.pack("<Ii", idx + 1, idx + 1)
        else:
            payload = struct.pack("<I", idx + 1)
        _write_slot(data, 0, h, idx, payload[:rec_size].ljust(rec_size, b"\x00"))
    return data


def _dense_table(n_live: int, *, rec_size: int = 4) -> bytearray:
    stride = 1 + rec_size + 2
    table_size = table_size_for_slots(
        Header(0, 1, 0, 0, rec_size, stride, 0, 0xFFFFFFFF, HEADER_SPAN),
        n_live,
    )
    h = Header(
        flags=0,
        seq=1,
        n_slots=n_live,
        n_live=n_live,
        rec_size=rec_size,
        slot_stride=stride,
        table_size=table_size,
        free_head=0xFFFFFFFF,
        data_offset=HEADER_SPAN,
    )
    data = bytearray(table_size)
    _publish_header(data, 0, h)
    for idx in range(n_live):
        payload = struct.pack("<I", idx + 1)
        _write_slot(data, 0, h, idx, payload[:rec_size].ljust(rec_size, b"\x00"))
    return data


class EdbLargeFileTests(unittest.TestCase):
    def test_check_large_sparse_table(self) -> None:
        data = _sparse_table(LARGE_SLOT_COUNT)
        issues, h = check_table(bytes(data), 0)
        errors = [i for i in issues if i.severity == "error"]
        self.assertEqual(errors, [])
        assert h is not None
        self.assertEqual(h.n_slots, LARGE_SLOT_COUNT)
        self.assertEqual(h.n_live, LARGE_SLOT_COUNT // 2)

    def test_vacuum_large_sparse_table(self) -> None:
        data = _sparse_table(LARGE_SLOT_COUNT, stable_ids=True)
        out, remaps, h2 = vacuum_table(bytes(data), 0)
        assert h2 is not None
        self.assertEqual(h2.n_slots, LARGE_SLOT_COUNT // 2)
        self.assertEqual(h2.n_live, LARGE_SLOT_COUNT // 2)
        self.assertEqual(len(remaps), h2.n_live - 1)
        issues, _ = check_table(out, 0)
        self.assertEqual([i for i in issues if i.severity == "error"], [])

    def test_grow_megabyte_table(self) -> None:
        rec_size = 8
        stride = 1 + rec_size + 2
        initial_slots = (MEGABYTE - HEADER_SPAN) // stride
        data = _dense_table(initial_slots // 4, rec_size=rec_size)
        active = active_header(bytes(data), 0)
        assert active is not None
        h, _ = active
        result = grow_table(bytes(data), 0, new_table_size=MEGABYTE)
        self.assertEqual(result.new_table_size, MEGABYTE)
        self.assertGreater(result.header.max_slots(), h.max_slots())
        issues, h3 = check_table(result.data, 0)
        self.assertEqual([i for i in issues if i.severity == "error"], [])
        assert h3 is not None
        self.assertEqual(h3.n_live, h.n_live)

    def test_scan_multi_table_megabyte_file(self) -> None:
        rec_size = 4
        stride = 1 + rec_size + 2
        slots_a = 4096
        size_a = table_size_for_slots(
            Header(0, 1, 0, 0, rec_size, stride, 0, 0xFFFFFFFF, HEADER_SPAN),
            slots_a,
        )
        slots_b = 2048
        size_b = table_size_for_slots(
            Header(0, 1, 0, 0, rec_size, stride, 0, 0xFFFFFFFF, HEADER_SPAN),
            slots_b,
        )
        blob = bytearray(size_a + size_b + MEGABYTE)
        table_a = _dense_table(slots_a, rec_size=rec_size)
        table_b = _dense_table(slots_b, rec_size=rec_size)
        blob[0:size_a] = table_a
        blob[size_a : size_a + size_b] = table_b

        offsets = scan_v3_table_offsets(bytes(blob), 0)
        self.assertEqual(offsets, [0, size_a])

    def test_check_ring_table_at_scale(self) -> None:
        rec_size = 4
        stride = 1 + rec_size + 2
        cap = 4096
        table_size = table_size_for_slots(
            Header(0, 1, 0, 0, rec_size, stride, 0, 0xFFFFFFFF, HEADER_SPAN),
            cap,
        )
        h = Header(
            flags=HDR_RING,
            seq=1,
            n_slots=cap,
            n_live=cap,
            rec_size=rec_size,
            slot_stride=stride,
            table_size=table_size,
            free_head=0xFFFFFFFF,
            data_offset=HEADER_SPAN,
            ring_head=0,
        )
        data = bytearray(table_size)
        _publish_header(data, 0, h)
        for idx in range(cap):
            payload = struct.pack("<I", idx + 1)
            _write_slot(data, 0, h, idx, payload)
        issues, h2 = check_table(bytes(data), 0)
        self.assertEqual([i for i in issues if i.severity == "error"], [])
        assert h2 is not None
        self.assertTrue(h2.ring_mode())

    def test_file_bytes_match_table_size(self) -> None:
        data = _sparse_table(5000)
        h = check_table(bytes(data), 0)[1]
        assert h is not None
        self.assertEqual(len(data), h.table_size)
        self.assertGreater(len(data), 30_000)


if __name__ == "__main__":
    unittest.main()
