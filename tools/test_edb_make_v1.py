#!/usr/bin/env python3
"""Tests for tools/edb_make_v1.py (legacy v1 file generator)."""

from __future__ import annotations

import json
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from edb_make_v1 import gen_records, load_dataset, make_v1, verify_dataset_bin  # noqa: E402
from edb_migrate import migrate_bytes, read_header  # noqa: E402
from gen_datasets import fnv1a64  # noqa: E402

REPO_DATA = TOOLS_DIR.parent / "test" / "data"


def write_fake_dataset(out_dir: Path, records: bytes) -> Path:
    """Write a minimal gen_datasets-style manifest + .bin into out_dir; return the manifest path."""
    (out_dir / "sensorlog.bin").write_bytes(records)
    manifest = {
        "record_size": 22,
        "count": len(records) // 22,
        "files": {"records": {"name": "sensorlog.bin", "bytes": len(records),
                              "fnv1a64": "%016x" % fnv1a64(records)}},
    }
    path = out_dir / "sensorlog.json"
    path.write_text(json.dumps(manifest))
    return path


def v3_live_records(data: bytes) -> bytes:
    import struct
    n_slots = struct.unpack_from("<I", data, 8)[0]
    rec_size = struct.unpack_from("<H", data, 16)[0]
    stride = struct.unpack_from("<H", data, 18)[0]
    offset = struct.unpack_from("<H", data, 28)[0]
    out = bytearray()
    for i in range(n_slots):
        base = offset + i * stride
        out.extend(data[base + 1 : base + 1 + rec_size])
    return bytes(out)


class GenRecordsTests(unittest.TestCase):
    def test_deterministic_for_seed(self) -> None:
        a = gen_records(8, 22, 0x1234ABCD)
        b = gen_records(8, 22, 0x1234ABCD)
        self.assertEqual(a, b)
        self.assertEqual(len(a), 8 * 22)

    def test_different_seed_differs(self) -> None:
        self.assertNotEqual(gen_records(8, 22, 1), gen_records(8, 22, 2))


class MakeV1Tests(unittest.TestCase):
    def test_generates_parseable_v1(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "legacy.db"
            make_v1(out, "avr", rec_size=22, count=64, table_size=None,
                    seed=0x1234ABCD, from_raw=None, force=False)
            header = read_header(out.read_bytes(), "avr")
            self.assertEqual(header.version, 1)
            self.assertEqual(header.n_recs, 64)
            self.assertEqual(header.rec_size, 22)

    def test_survives_migration_to_v3(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "legacy.db"
            make_v1(out, "esp32", rec_size=8, count=10, table_size=None,
                    seed=42, from_raw=None, force=False)
            expected = gen_records(10, 8, 42)
            v3, header, changed = migrate_bytes(out.read_bytes(), "esp32")
            self.assertTrue(changed)
            self.assertEqual(header.version, 1)
            self.assertEqual(v3_live_records(v3), expected)

    def test_from_raw_copies_bytes(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            raw = Path(tmp) / "raw.bin"
            payload = bytes(range(256)) * 2  # 512 bytes
            raw.write_bytes(payload)
            out = Path(tmp) / "legacy.db"
            make_v1(out, "avr", rec_size=4, count=8, table_size=None,
                    seed=0, from_raw=raw, force=False)
            header = read_header(out.read_bytes(), "avr")
            body = out.read_bytes()[header.header_size : header.header_size + 32]
            self.assertEqual(body, payload[:32])

    def test_refuses_overwrite_without_force(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "legacy.db"
            out.write_bytes(b"existing")
            with self.assertRaises(FileExistsError):
                make_v1(out, "avr", rec_size=4, count=2, table_size=None,
                        seed=0, from_raw=None, force=False)


class FromDatasetTests(unittest.TestCase):
    def test_load_dataset_from_dir(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            records = bytes(range(22)) * 3
            write_fake_dataset(Path(tmp), records)
            manifest, bin_path = load_dataset(Path(tmp))
            self.assertEqual(manifest["record_size"], 22)
            self.assertEqual(manifest["count"], 3)
            self.assertEqual(bin_path.read_bytes(), records)

    def test_verify_dataset_bin_detects_wrong_size(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            records = bytes(range(22)) * 3
            manifest_path = write_fake_dataset(Path(tmp), records)
            manifest = json.loads(manifest_path.read_text())
            with self.assertRaises(ValueError):
                verify_dataset_bin(manifest, records[:-1])

    def test_cli_from_dataset_autofills(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            records = bytes((i * 7) & 0xFF for i in range(22 * 5))
            write_fake_dataset(Path(tmp), records)
            out = Path(tmp) / "legacy.db"
            result = subprocess.run(
                [sys.executable, str(TOOLS_DIR / "edb_make_v1.py"), str(out),
                 "--from-dataset", tmp, "--arch", "avr"],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            header = read_header(out.read_bytes(), "avr")
            self.assertEqual(header.rec_size, 22)
            self.assertEqual(header.n_recs, 5)
            body = out.read_bytes()[header.header_size : header.header_size + 22 * 5]
            self.assertEqual(body, records)

    def test_cli_from_dataset_rejects_conflicting_rec_size(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            write_fake_dataset(Path(tmp), bytes(range(22)) * 2)
            out = Path(tmp) / "legacy.db"
            result = subprocess.run(
                [sys.executable, str(TOOLS_DIR / "edb_make_v1.py"), str(out),
                 "--from-dataset", tmp, "--rec-size", "8"],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 1)
            self.assertIn("conflicts", result.stderr)

    @unittest.skipUnless((REPO_DATA / "sensorlog.json").exists(),
                         "shipped dataset absent; run tools/gen_datasets.py")
    def test_shipped_dataset_survives_to_v3(self) -> None:
        manifest, bin_path = load_dataset(REPO_DATA)
        src = bin_path.read_bytes()[:64 * manifest["record_size"]]
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "legacy.db"
            # 64-record subset keeps the file within the avr 16-bit table_size field
            make_v1(out, "avr", rec_size=manifest["record_size"], count=64, table_size=None,
                    seed=0, from_raw=bin_path, force=False)
            v3, _, _ = migrate_bytes(out.read_bytes(), "avr")
            self.assertEqual(v3_live_records(v3), src)


class MakeV1CliTests(unittest.TestCase):
    def test_cli_make_then_migrate(self) -> None:
        with tempfile.TemporaryDirectory() as tmp:
            legacy = Path(tmp) / "legacy.db"
            v3 = Path(tmp) / "out_v3.db"
            gen = subprocess.run(
                [sys.executable, str(TOOLS_DIR / "edb_make_v1.py"), str(legacy),
                 "--arch", "avr", "--rec-size", "22", "--count", "32"],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(gen.returncode, 0, msg=gen.stderr)
            mig = subprocess.run(
                [sys.executable, str(TOOLS_DIR / "edb_migrate.py"), str(legacy), str(v3)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(mig.returncode, 0, msg=mig.stderr)
            self.assertEqual(v3_live_records(v3.read_bytes()), gen_records(32, 22, 0x1234ABCD))


if __name__ == "__main__":
    unittest.main()
