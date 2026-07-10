#!/usr/bin/env python3
"""Tests for tools/edb_v3_format.py, the single Python source of truth for the v3 format.

Both host consumers (tools/edb_v3_io.py and the gateway's edb_v3.py) import this module, so these
tests guard the primitives they share -- in particular the header pack/parse round-trip of
next_record_id and ring_head, whose omission was the drift that motivated the shared module.
"""

from __future__ import annotations

import struct
import sys
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from edb_v3_format import (  # noqa: E402
    FREE_NONE,
    HDR_RING,
    HDR_STABLE_IDS,
    SLOT_LIVE,
    SLOT_TOMBSTONE,
    Header,
    crc16_ccitt_edb,
    crc32_edb,
    pack_header,
    parse_header_copy,
    publish,
    read_slot_raw,
    select_active,
    slot_offset,
    write_slot,
)


def make_header(**kw) -> Header:
    base = dict(flags=0, seq=1, n_slots=0, n_live=0, rec_size=8, slot_stride=1 + 8 + 2,
                table_size=96 + 64 * (1 + 8 + 2), free_head=FREE_NONE, data_offset=96)
    base.update(kw)
    return Header(**base)


class ChecksumTests(unittest.TestCase):
    def test_crc32_known_vector(self) -> None:
        # standard reflected CRC-32 of b"123456789"
        self.assertEqual(crc32_edb(b"123456789"), 0xCBF43926)

    def test_crc16_ccitt_false_known_vector(self) -> None:
        # CRC-16/CCITT-FALSE (init 0xFFFF, poly 0x1021) of b"123456789"
        self.assertEqual(crc16_ccitt_edb(b"123456789"), 0x29B1)


class HeaderRoundTripTests(unittest.TestCase):
    def test_pack_parse_preserves_all_fields(self) -> None:
        h = make_header(flags=HDR_STABLE_IDS | HDR_RING, seq=7, n_slots=5, n_live=4,
                        free_head=3, next_record_id=42, ring_head=2)
        got = parse_header_copy(pack_header(h))
        self.assertIsNotNone(got)
        for field in ("flags", "seq", "n_slots", "n_live", "rec_size", "slot_stride",
                      "table_size", "free_head", "data_offset", "next_record_id", "ring_head"):
            self.assertEqual(getattr(got, field), getattr(h, field), field)

    def test_next_record_id_and_ring_head_survive(self) -> None:
        # The exact drift the shared module fixes: both must be packed into reserved and read back.
        packed = pack_header(make_header(next_record_id=1000, ring_head=17))
        self.assertEqual(struct.unpack_from("<I", packed, 30)[0], 1000)  # reserved[0..3]
        self.assertEqual(struct.unpack_from("<I", packed, 34)[0], 17)    # reserved[4..7]

    def test_bad_crc_rejected(self) -> None:
        packed = bytearray(pack_header(make_header()))
        packed[8] ^= 0xFF  # corrupt n_slots without fixing the CRC
        self.assertIsNone(parse_header_copy(bytes(packed)))


class SelectActiveTests(unittest.TestCase):
    def test_picks_higher_seq(self) -> None:
        data = bytearray(pack_header(make_header(seq=1)) + pack_header(make_header(seq=9)))
        h, copy, legacy = select_active(data, 0)
        self.assertEqual((copy, legacy), (1, False))
        self.assertEqual(h.seq, 9)

    def test_self_heal_when_one_copy_corrupt(self) -> None:
        good = pack_header(make_header(seq=4))
        data = bytearray(b"\x00" * 48 + good)  # copy 0 invalid, copy 1 good
        h, copy, legacy = select_active(data, 0)
        self.assertEqual((copy, legacy), (1, False))
        self.assertEqual(h.seq, 4)

    def test_legacy_detected(self) -> None:
        data = bytearray(b"\xDB\x01" + b"\x00" * 94)  # magic + version 1
        h, copy, legacy = select_active(data, 0)
        self.assertEqual((h, legacy), (None, True))


class SlotTests(unittest.TestCase):
    def test_write_then_read_live(self) -> None:
        h = make_header(n_slots=1)
        data = bytearray(h.table_size)
        write_slot(data, 0, h, 0, b"ABCDEFGH")
        res = read_slot_raw(data, 0, h, 0)
        self.assertEqual(res, (SLOT_LIVE, b"ABCDEFGH", True))

    def test_corrupt_detected(self) -> None:
        h = make_header(n_slots=1)
        data = bytearray(h.table_size)
        write_slot(data, 0, h, 0, b"ABCDEFGH")
        data[slot_offset(0, h, 0) + 1] ^= 0xFF  # flip a payload byte, leave CRC stale
        status, _payload, crc_ok = read_slot_raw(data, 0, h, 0)
        self.assertEqual(status, SLOT_LIVE)
        self.assertFalse(crc_ok)

    def test_tombstone_and_truncated(self) -> None:
        h = make_header(n_slots=2)
        data = bytearray(h.table_size)
        data[slot_offset(0, h, 0)] = SLOT_TOMBSTONE
        self.assertEqual(read_slot_raw(data, 0, h, 0), (SLOT_TOMBSTONE, b"", False))
        short = bytearray(slot_offset(0, h, 1) + 3)  # cut off inside slot 1
        self.assertIsNone(read_slot_raw(short, 0, h, 1))


class PublishTests(unittest.TestCase):
    def test_publish_targets_other_copy_and_bumps_seq(self) -> None:
        data = bytearray(pack_header(make_header(seq=1)) + pack_header(make_header(seq=1)))
        h, copy, _ = select_active(data, 0)
        target = publish(data, 0, h, copy)
        self.assertEqual(target, 1 - copy)
        again, new_copy, _ = select_active(data, 0)
        self.assertEqual(again.seq, 2)
        self.assertEqual(new_copy, target)


if __name__ == "__main__":
    unittest.main()
