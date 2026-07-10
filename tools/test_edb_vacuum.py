"""Tests for edb_v3_io check/vacuum helpers."""

from __future__ import annotations

import struct
import tempfile
import unittest
from pathlib import Path

from edb_v3_io import (
    HEADER_SPAN,
    HDR_STABLE_IDS,
    SLOT_LIVE,
    SLOT_TOMBSTONE,
    Header,
    _pack_header,
    _write_slot,
    check_table,
    crc16_ccitt_edb,
    vacuum_table,
)


def _make_table(
    rec_size: int,
    table_size: int,
    *,
    stable_ids: bool = False,
    next_record_id: int = 1,
) -> bytearray:
    stride = 1 + rec_size + 2
    flags = HDR_STABLE_IDS if stable_ids else 0
    h = Header(
        flags=flags,
        seq=1,
        n_slots=0,
        n_live=0,
        rec_size=rec_size,
        slot_stride=stride,
        table_size=table_size,
        free_head=0xFFFFFFFF,
        data_offset=HEADER_SPAN,
        next_record_id=next_record_id,
    )
    data = bytearray(b"\x00" * table_size)
    packed = _pack_header(h)
    data[0:HEADER_SPAN] = packed + packed
    return data


class EdbV3IoTests(unittest.TestCase):
    def test_check_ok_empty(self) -> None:
        data = _make_table(8, 512)
        issues, h = check_table(data, 0)
        self.assertEqual(issues, [])
        self.assertIsNotNone(h)
        self.assertEqual(h.n_slots, 0)

    def test_vacuum_repacks_after_deletes(self) -> None:
        rec_size = 8
        data = _make_table(rec_size, 512, stable_ids=True, next_record_id=1)
        h = Header(
            flags=HDR_STABLE_IDS,
            seq=1,
            n_slots=4,
            n_live=2,
            rec_size=rec_size,
            slot_stride=1 + rec_size + 2,
            table_size=512,
            free_head=0xFFFFFFFF,
            data_offset=HEADER_SPAN,
            next_record_id=5,
        )
        packed = _pack_header(h)
        data[0:HEADER_SPAN] = packed + packed

        payloads = []
        for rid, val in ((1, 100), (3, 300)):
            payload = struct.pack("<Ii", rid, val)
            payloads.append(payload)
            idx = rid - 1
            _write_slot(data, 0, h, idx, payload)

        # tombstones at slots 2 and 4 (recno 2, 4)
        data[HEADER_SPAN + 1 * h.slot_stride] = SLOT_TOMBSTONE
        data[HEADER_SPAN + 3 * h.slot_stride] = SLOT_TOMBSTONE

        out, remaps, h2 = vacuum_table(bytes(data), 0)
        self.assertIsNotNone(h2)
        assert h2 is not None
        self.assertEqual(h2.n_slots, 2)
        self.assertEqual(h2.n_live, 2)
        self.assertEqual(len(remaps), 1)
        self.assertEqual(remaps[0].old_recno, 3)
        self.assertEqual(remaps[0].new_recno, 2)
        self.assertEqual(remaps[0].record_id, 3)

        issues, _ = check_table(out, 0)
        self.assertEqual([i for i in issues if i.severity == "error"], [])

    def test_vacuum_cli_roundtrip(self) -> None:
        rec_size = 4
        data = _make_table(rec_size, 256)
        h = Header(
            flags=0,
            seq=1,
            n_slots=3,
            n_live=2,
            rec_size=rec_size,
            slot_stride=7,
            table_size=256,
            free_head=0xFFFFFFFF,
            data_offset=HEADER_SPAN,
        )
        packed = _pack_header(h)
        data[0:HEADER_SPAN] = packed + packed
        _write_slot(data, 0, h, 0, b"\x01\x00\x00\x00")
        _write_slot(data, 0, h, 2, b"\x03\x00\x00\x00")
        data[HEADER_SPAN + 1 * h.slot_stride] = SLOT_TOMBSTONE

        out, remaps, h2 = vacuum_table(bytes(data), 0)
        assert h2 is not None
        self.assertEqual(h2.n_slots, 2)
        self.assertEqual(len(remaps), 1)

        with tempfile.TemporaryDirectory() as td:
            src = Path(td) / "in.db"
            dst = Path(td) / "out.db"
            src.write_bytes(data)
            import subprocess
            import sys

            subprocess.check_call(
                [sys.executable, str(Path(__file__).parent / "edb_vacuum.py"), str(src), str(dst), "--force"],
            )
            self.assertTrue(dst.exists())
            issues, h3 = check_table(dst.read_bytes(), 0)
            self.assertEqual([i for i in issues if i.severity == "error"], [])
            self.assertEqual(h3.n_slots, 2)


if __name__ == "__main__":
    unittest.main()
