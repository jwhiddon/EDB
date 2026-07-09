#!/usr/bin/env python3
"""Tests for tools/edb_migrate.py (v1/v2 -> v3)."""

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
    V2_HEADER_FMT,
    V2_VERSION,
    V3_HEADER_COPY_SIZE,
    V3_SLOT_LIVE,
    V3_VERSION,
    crc16_ccitt_edb,
    crc32_edb,
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
    out = bytes(header) + b"".join(records)
    if len(out) < TABLE_SIZE:
        out += b"\xFF" * (TABLE_SIZE - len(out))
    return out


def build_v1_esp32(n_recs: int, records: list[bytes]) -> bytes:
    header = bytearray(16)
    header[0] = EDB_FLAG
    struct.pack_into("<I", header, 4, n_recs)
    struct.pack_into("<H", header, 8, REC_SIZE)
    struct.pack_into("<I", header, 12, TABLE_SIZE)
    out = bytes(header) + b"".join(records)
    if len(out) < TABLE_SIZE:
        out += b"\xFF" * (TABLE_SIZE - len(out))
    return out


def build_v2(n_recs: int, records: list[bytes]) -> bytes:
    header = struct.pack(V2_HEADER_FMT, EDB_FLAG, V2_VERSION, n_recs, REC_SIZE, TABLE_SIZE)
    out = header + b"".join(records)
    if len(out) < TABLE_SIZE:
        out += b"\xFF" * (TABLE_SIZE - len(out))
    return out


def parse_v3(data: bytes) -> tuple[list[bytes], int, int]:
    """Validate the v3 file structure and return (live_records, n_live, rec_size)."""
    assert len(data) >= 2 * V3_HEADER_COPY_SIZE
    c0 = data[0:V3_HEADER_COPY_SIZE]
    c1 = data[V3_HEADER_COPY_SIZE : 2 * V3_HEADER_COPY_SIZE]
    assert c0[0] == EDB_FLAG and c0[1] == V3_VERSION
    assert struct.unpack_from("<I", c0, 44)[0] == crc32_edb(bytes(c0[0:44])), "header CRC32"
    assert c0 == c1, "both header copies must match after migration"
    n_slots = struct.unpack_from("<I", c0, 8)[0]
    n_live = struct.unpack_from("<I", c0, 12)[0]
    rec_size = struct.unpack_from("<H", c0, 16)[0]
    slot_stride = struct.unpack_from("<H", c0, 18)[0]
    data_offset = struct.unpack_from("<H", c0, 28)[0]
    records: list[bytes] = []
    for i in range(n_slots):
        off = data_offset + i * slot_stride
        status = data[off]
        payload = data[off + 1 : off + 1 + rec_size]
        stored_crc = struct.unpack_from("<H", data, off + 1 + rec_size)[0]
        assert status == V3_SLOT_LIVE
        assert stored_crc == crc16_ccitt_edb(bytes([status]) + payload), "slot CRC16"
        records.append(payload)
    return records, n_live, rec_size


class MigrateBytesTests(unittest.TestCase):
    def test_migrate_avr_v1_to_v3(self) -> None:
        records = [struct.pack("<I", v) for v in (1, 2, 3)]
        migrated, header, changed = migrate_bytes(build_v1_avr(3, records), "avr")
        self.assertTrue(changed)
        self.assertEqual(header.version, 1)
        out_records, n_live, rec_size = parse_v3(migrated)
        self.assertEqual(out_records, records)
        self.assertEqual(n_live, 3)
        self.assertEqual(rec_size, REC_SIZE)

    def test_migrate_esp32_v1_to_v3(self) -> None:
        records = [struct.pack("<I", v) for v in (9, 8)]
        migrated, header, changed = migrate_bytes(build_v1_esp32(2, records), "esp32")
        self.assertTrue(changed)
        out_records, n_live, _ = parse_v3(migrated)
        self.assertEqual(out_records, records)
        self.assertEqual(n_live, 2)

    def test_migrate_v2_to_v3(self) -> None:
        records = [struct.pack("<I", v) for v in (5, 6, 7, 8)]
        migrated, header, changed = migrate_bytes(build_v2(4, records), "auto")
        self.assertTrue(changed)
        self.assertEqual(header.version, 2)
        out_records, n_live, _ = parse_v3(migrated)
        self.assertEqual(out_records, records)
        self.assertEqual(n_live, 4)

    def test_already_v3_is_noop(self) -> None:
        records = [struct.pack("<I", 7)]
        v3, _, _ = migrate_bytes(build_v1_avr(1, records), "avr")
        again, header, changed = migrate_bytes(v3, "auto")
        self.assertFalse(changed)
        self.assertEqual(header.version, 3)
        self.assertEqual(again, v3)

    def test_invalid_flag_raises(self) -> None:
        bad = bytes([0x00]) + build_v1_avr(1, [struct.pack("<I", 1)])[1:]
        with self.assertRaises(ValueError):
            migrate_bytes(bad, "avr")

    def test_truncated_file_raises(self) -> None:
        with self.assertRaises(ValueError):
            migrate_bytes(bytes([EDB_FLAG]), "auto")

    def test_invalid_nrecs_raises(self) -> None:
        source = bytearray(build_v1_avr(1, [struct.pack("<I", 1)]))
        struct.pack_into("<I", source, 4, 9999)
        with self.assertRaises(ValueError):
            migrate_bytes(bytes(source), "avr")

    def test_capacity_preserved(self) -> None:
        records = [struct.pack("<I", 1)]
        migrated, _, _ = migrate_bytes(build_v1_avr(1, records), "avr")
        # original AVR capacity = (64-12)/4 = 13 slots -> v3 table_size = 96 + 13*7
        table_size = struct.unpack_from("<I", migrated, 20)[0]
        self.assertEqual(table_size, 96 + 13 * (1 + REC_SIZE + 2))


class MigrateCliTests(unittest.TestCase):
    def test_cli_round_trip(self) -> None:
        records = [struct.pack("<I", v) for v in (4, 5, 6)]
        source = build_v1_avr(3, records)
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "input.db"
            output_path = Path(tmp) / "output.db"
            input_path.write_bytes(source)
            result = subprocess.run(
                [sys.executable, str(TOOLS_DIR / "edb_migrate.py"),
                 str(input_path), str(output_path), "--arch", "avr"],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 0, msg=result.stderr)
            out_records, _, _ = parse_v3(output_path.read_bytes())
            self.assertEqual(out_records, records)

    def test_cli_refuses_overwrite_without_force(self) -> None:
        source = build_v1_avr(1, [struct.pack("<I", 1)])
        with tempfile.TemporaryDirectory() as tmp:
            input_path = Path(tmp) / "input.db"
            output_path = Path(tmp) / "output.db"
            input_path.write_bytes(source)
            output_path.write_bytes(b"existing")
            result = subprocess.run(
                [sys.executable, str(TOOLS_DIR / "edb_migrate.py"), str(input_path), str(output_path)],
                capture_output=True, text=True, check=False,
            )
            self.assertEqual(result.returncode, 1)

    def test_in_place_migration(self) -> None:
        records = [struct.pack("<I", v) for v in (2, 3)]
        source = build_v1_avr(2, records)
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "db.db"
            path.write_bytes(source)
            status = migrate_file(path, path, "avr", force=False)
            self.assertEqual(status, 0)
            out_records, _, _ = parse_v3(path.read_bytes())
            self.assertEqual(out_records, records)


if __name__ == "__main__":
    unittest.main()
