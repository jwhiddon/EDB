#!/usr/bin/env python3
"""Tests for tools/edb_migrate.py"""

from __future__ import annotations

import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(TOOLS_DIR))

from edb_migrate import (  # noqa: E402
    EDB_FLAG,
    EDB_VERSION,
    V2_HEADER_FMT,
    migrate_bytes,
    migrate_file,
    read_header,
)

REC_SIZE = 4
TABLE_SIZE = 64


def build_v1_avr(n_recs: int, records: list[bytes]) -> bytes:
    header = bytearray(12)
    header[0] = EDB_FLAG
    struct.pack_into("<I", header, 4, n_recs)
    struct.pack_into("<H", header, 8, REC_SIZE)
    struct.pack_into("<H", header, 10, TABLE_SIZE)
    body = b"".join(records)
    out = bytes(header) + body
    if len(out) < TABLE_SIZE:
        out += b"\xFF" * (TABLE_SIZE - len(out))
    return out


def build_v1_esp32(n_recs: int, records: list[bytes]) -> bytes:
    header = bytearray(16)
    header[0] = EDB_FLAG
    struct.pack_into("<I", header, 4, n_recs)
    struct.pack_into("<H", header, 8, REC_SIZE)
    struct.pack_into("<I", header, 12, TABLE_SIZE)
    body = b"".join(records)
    out = bytes(header) + body
    if len(out) < TABLE_SIZE:
        out += b"\xFF" * (TABLE_SIZE - len(out))
    return out


def build_v2(n_recs: int, records: list[bytes]) -> bytes:
    header = struct.pack(V2_HEADER_FMT, EDB_FLAG, EDB_VERSION, n_recs, REC_SIZE, TABLE_SIZE)
    body = b"".join(records)
    out = header + body
    if len(out) < TABLE_SIZE:
        out += b"\xFF" * (TABLE_SIZE - len(out))
    return out


def record_region(data: bytes, header_size: int, n_recs: int, rec_size: int) -> bytes:
    end = header_size + n_recs * rec_size
    return data[header_size:end]


class MigrateBytesTests(unittest.TestCase):
    def assert_record_regions_match(
        self,
        source: bytes,
        migrated: bytes,
        old_header_size: int,
        n_recs: int,
    ) -> None:
        self.assertEqual(
            record_region(source, old_header_size, n_recs, REC_SIZE),
            record_region(migrated, 12, n_recs, REC_SIZE),
        )

    def test_migrate_avr_v1_to_v2(self) -> None:
        records = [struct.pack("<I", value) for value in (1, 2, 3)]
        source = build_v1_avr(3, records)
        migrated, header, changed = migrate_bytes(source, "avr")
        self.assertTrue(changed)
        self.assertEqual(header.n_recs, 3)
        self.assertEqual(migrated[0], EDB_FLAG)
        self.assertEqual(migrated[1], EDB_VERSION)
        self.assert_record_regions_match(source, migrated, 12, 3)

    def test_migrate_esp32_v1_to_v2(self) -> None:
        records = [struct.pack("<I", value) for value in (9, 8)]
        source = build_v1_esp32(2, records)
        migrated, header, changed = migrate_bytes(source, "esp32")
        self.assertTrue(changed)
        self.assertEqual(header.n_recs, 2)
        self.assert_record_regions_match(source, migrated, 16, 2)

    def test_auto_detect_avr(self) -> None:
        records = [struct.pack("<I", 42)]
        source = build_v1_avr(1, records)
        header = read_header(source, "auto")
        self.assertFalse(header.is_v2)
        self.assertEqual(header.n_recs, 1)

    def test_already_v2_is_noop(self) -> None:
        records = [struct.pack("<I", 7)]
        source = build_v2(1, records)
        migrated, header, changed = migrate_bytes(source, "auto")
        self.assertFalse(changed)
        self.assertTrue(header.is_v2)
        self.assertEqual(migrated, source)

    def test_invalid_flag_raises(self) -> None:
        source = build_v1_avr(1, [struct.pack("<I", 1)])
        bad = bytes([0x00]) + source[1:]
        with self.assertRaises(ValueError):
            migrate_bytes(bad, "avr")

    def test_truncated_file_raises(self) -> None:
        with self.assertRaises(ValueError):
            migrate_bytes(bytes([EDB_FLAG]), "auto")

    def test_migrate_preserves_many_records(self) -> None:
        table_size = 128
        records = [struct.pack("<I", i * 11) for i in range(1, 21)]
        header = bytearray(12)
        header[0] = EDB_FLAG
        struct.pack_into("<I", header, 4, 20)
        struct.pack_into("<H", header, 8, REC_SIZE)
        struct.pack_into("<H", header, 10, table_size)
        source = bytes(header) + b"".join(records)
        if len(source) < table_size:
            source += b"\xFF" * (table_size - len(source))
        migrated, header, changed = migrate_bytes(source, "avr")
        self.assertTrue(changed)
        self.assertEqual(header.n_recs, 20)
        self.assert_record_regions_match(source, migrated, 12, 20)

    def test_migrate_preserves_padding(self) -> None:
        records = [struct.pack("<I", 1)]
        source = build_v1_avr(1, records)
        migrated, _, _ = migrate_bytes(source, "avr")
        self.assertEqual(migrated[TABLE_SIZE - 1], 0xFF)

    def test_migrate_avr_full_table(self) -> None:
        limit = (TABLE_SIZE - 12) // REC_SIZE
        records = [struct.pack("<I", i) for i in range(limit)]
        source = build_v1_avr(limit, records)
        migrated, header, changed = migrate_bytes(source, "avr")
        self.assertTrue(changed)
        self.assertEqual(header.n_recs, limit)
        self.assert_record_regions_match(source, migrated, 12, limit)

    def test_migrate_esp32_full_table(self) -> None:
        limit = (TABLE_SIZE - 16) // REC_SIZE
        records = [struct.pack("<I", i) for i in range(limit)]
        source = build_v1_esp32(limit, records)
        migrated, header, changed = migrate_bytes(source, "esp32")
        self.assertTrue(changed)
        self.assertEqual(header.n_recs, limit)
        self.assert_record_regions_match(source, migrated, 16, limit)

    def test_invalid_nrecs_does_not_write_output(self) -> None:
        source = bytearray(build_v1_avr(1, [struct.pack("<I", 1)]))
        struct.pack_into("<I", source, 4, 9999)
        with self.assertRaises(ValueError):
            migrate_bytes(bytes(source), "avr")


class MigrateCliTests(unittest.TestCase):
    def test_cli_round_trip(self) -> None:
        records = [struct.pack("<I", value) for value in (4, 5, 6)]
        source = build_v1_avr(3, records)
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "input.db"
            output_path = Path(tmp) / "output.db"
            input_path.write_bytes(source)
            result = subprocess.run(
                [sys.executable, str(TOOLS_DIR / "edb_migrate.py"), str(input_path), str(output_path), "--arch", "avr"],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            migrated = output_path.read_bytes()
            self.assertEqual(migrated[1], EDB_VERSION)
            self.assertEqual(migrated[12:24], b"".join(records))

    def test_cli_refuses_overwrite_without_force(self) -> None:
        records = [struct.pack("<I", 1)]
        source = build_v1_avr(1, records)
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "input.db"
            output_path = Path(tmp) / "output.db"
            input_path.write_bytes(source)
            output_path.write_bytes(b"existing")
            result = subprocess.run(
                [sys.executable, str(TOOLS_DIR / "edb_migrate.py"), str(input_path), str(output_path)],
                capture_output=True,
                text=True,
                check=False,
            )
            self.assertEqual(result.returncode, 1)

    def test_migrate_file_already_v2(self) -> None:
        records = [struct.pack("<I", 2)]
        source = build_v2(1, records)
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "input.db"
            output_path = Path(tmp) / "output.db"
            input_path.write_bytes(source)
            status = migrate_file(input_path, output_path, "auto", False)
            self.assertEqual(status, 0)
            self.assertEqual(output_path.read_bytes(), source)


if __name__ == "__main__":
    unittest.main()
