#!/usr/bin/env python3
"""Verify the shipped test datasets are consistent and reproducible.

The generator is deterministic, so the committed test/data/*.bin must match a fresh regeneration and
the recorded hashes in test/data/sensorlog.json. This guards against drift and proves the shipped
data is what the tests expect.
"""

from __future__ import annotations

import json
import struct
import sys
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

import gen_datasets as gd  # noqa: E402

DATA_DIR = TOOLS_DIR.parent / "test" / "data"


class DatasetTests(unittest.TestCase):
    def setUp(self) -> None:
        self.bin_path = DATA_DIR / "sensorlog.bin"
        self.upd_path = DATA_DIR / "sensorlog_updates.bin"
        self.json_path = DATA_DIR / "sensorlog.json"
        for p in (self.bin_path, self.upd_path, self.json_path):
            if not p.exists():
                self.skipTest(f"{p} not present; run tools/gen_datasets.py")
        self.manifest = json.loads(self.json_path.read_text())

    def test_manifest_matches_files(self):
        for key, path in (("records", self.bin_path), ("updates", self.upd_path)):
            data = path.read_bytes()
            self.assertEqual(len(data), self.manifest["files"][key]["bytes"])
            self.assertEqual("%016x" % gd.fnv1a64(data), self.manifest["files"][key]["fnv1a64"])

    def test_shipped_matches_regeneration(self):
        n = self.manifest["count"]
        base = gd.gen_records(n, gd.SEED_BASE)
        upd = gd.gen_updates(base, gd.SEED_UPDATE)
        self.assertEqual(gd.pack(base), self.bin_path.read_bytes(),
                         "test/data/sensorlog.bin is stale -- rerun tools/gen_datasets.py")
        self.assertEqual(gd.pack(upd), self.upd_path.read_bytes(),
                         "test/data/sensorlog_updates.bin is stale -- rerun tools/gen_datasets.py")

    def test_record_layout_and_known_values(self):
        self.assertEqual(gd.REC_SIZE, 22)
        data = self.bin_path.read_bytes()
        self.assertEqual(len(data) % gd.REC_SIZE, 0)
        rec0 = struct.unpack_from(gd.REC_FMT, data, 0)
        self.assertEqual(rec0[0], 1000)                       # id = 1000 + 0
        self.assertEqual(rec0[5], 0)                          # channel = 0 % 8
        self.assertEqual(rec0[6].rstrip(b"\x00"), b"kitchen")
        rec9 = struct.unpack_from(gd.REC_FMT, data, 9 * gd.REC_SIZE)
        self.assertEqual(rec9[0], 1009)
        self.assertEqual(rec9[5], 1)                          # channel = 9 % 8

    def test_updates_preserve_identity(self):
        base = struct.unpack_from(gd.REC_FMT, self.bin_path.read_bytes(), 0)
        upd = struct.unpack_from(gd.REC_FMT, self.upd_path.read_bytes(), 0)
        self.assertEqual(base[0], upd[0])   # id unchanged
        self.assertEqual(base[5], upd[5])   # channel unchanged
        self.assertEqual(base[6], upd[6])   # tag unchanged
        self.assertNotEqual(base[1], upd[1])  # timestamp advanced


if __name__ == "__main__":
    unittest.main()
