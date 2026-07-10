"""Tests for edb_grow.py / grow_table."""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from edb_v3_io import (
    HEADER_SPAN,
    Header,
    _pack_header,
    _write_slot,
    active_header,
    grow_table,
    scan_v3_table_offsets,
    table_size_for_slots,
)


def _empty_table(rec_size: int, table_size: int, head_ptr: int = 0) -> bytearray:
    stride = 1 + rec_size + 2
    h = Header(
        flags=0,
        seq=1,
        n_slots=0,
        n_live=0,
        rec_size=rec_size,
        slot_stride=stride,
        table_size=table_size,
        free_head=0xFFFFFFFF,
        data_offset=HEADER_SPAN,
    )
    data = bytearray(b"\x00" * (head_ptr + table_size))
    packed = _pack_header(h)
    data[head_ptr : head_ptr + HEADER_SPAN] = packed + packed
    return data


class EdbGrowTests(unittest.TestCase):
    def test_table_size_for_slots(self) -> None:
        h = Header(0, 1, 0, 0, 8, 11, 256, 0xFFFFFFFF, HEADER_SPAN)
        self.assertEqual(table_size_for_slots(h, 10), HEADER_SPAN + 10 * 11)

    def test_grow_add_slots(self) -> None:
        rec_size = 4
        stride = 7
        initial_slots = 4
        table_size = HEADER_SPAN + initial_slots * stride
        data = _empty_table(rec_size, table_size)
        h0, _ = active_header(bytes(data), 0)
        assert h0 is not None
        self.assertEqual(h0.max_slots(), initial_slots)

        result = grow_table(bytes(data), 0, add_slots=6)
        self.assertEqual(result.old_table_size, table_size)
        self.assertEqual(result.new_table_size, HEADER_SPAN + 10 * stride)
        self.assertEqual(result.header.max_slots(), 10)
        self.assertEqual(len(result.data), result.new_table_size)

    def test_grow_preserves_records(self) -> None:
        rec_size = 4
        stride = 7
        table_size = HEADER_SPAN + 4 * stride
        data = _empty_table(rec_size, table_size)
        h = Header(0, 1, 2, 2, rec_size, stride, table_size, 0xFFFFFFFF, HEADER_SPAN)
        packed = _pack_header(h)
        data[0:HEADER_SPAN] = packed + packed
        _write_slot(data, 0, h, 0, b"\x01\x00\x00\x00")
        _write_slot(data, 0, h, 1, b"\x02\x00\x00\x00")

        result = grow_table(bytes(data), 0, limit_slots=8)
        self.assertEqual(result.header.n_slots, 2)
        self.assertEqual(result.header.n_live, 2)
        self.assertEqual(result.data[HEADER_SPAN], 0xA5)
        self.assertEqual(bytes(result.data[HEADER_SPAN + 8 : HEADER_SPAN + 12]), b"\x02\x00\x00\x00")

    def test_grow_shifts_second_table(self) -> None:
        rec_size = 4
        stride = 7
        size_a = HEADER_SPAN + 4 * stride
        size_b = HEADER_SPAN + 2 * stride
        data = _empty_table(rec_size, size_a, 0)
        head_b = size_a
        data.extend(_empty_table(rec_size, size_b, head_b)[head_b : head_b + size_b])

        self.assertEqual(scan_v3_table_offsets(bytes(data)), [0, size_a])

        result = grow_table(bytes(data), 0, add_slots=4)
        delta = result.new_table_size - size_a
        self.assertEqual(scan_v3_table_offsets(bytes(result.data)), [0, size_a + delta])
        self.assertEqual(result.shifted_tables, [(size_a, size_a + delta)])

    def test_refuse_shrink(self) -> None:
        data = _empty_table(4, 256)
        with self.assertRaises(ValueError):
            grow_table(bytes(data), 0, new_table_size=128)

    def test_cli_roundtrip(self) -> None:
        data = _empty_table(4, HEADER_SPAN + 4 * 7)
        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "in.db"
            dst = Path(td) / "out.db"
            src.write_bytes(data)
            import subprocess
            import sys

            subprocess.check_call(
                [
                    sys.executable,
                    str(Path(__file__).parent / "edb_grow.py"),
                    str(src),
                    str(dst),
                    "--add-slots",
                    "8",
                    "--force",
                ],
            )
            out = dst.read_bytes()
            h, _ = active_header(out, 0)
            assert h is not None
            self.assertEqual(h.max_slots(), 12)


if __name__ == "__main__":
    unittest.main()
