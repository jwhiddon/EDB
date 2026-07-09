#!/usr/bin/env python3
"""Generate golden database fixtures for compatibility tests."""

from __future__ import annotations

import struct
from pathlib import Path

EDB_FLAG = 0xDB
REC_SIZE = 4

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "test" / "fixtures"


def build_v1_avr(n_recs: int, values: list[int], table_size: int) -> bytes:
    header = bytearray(12)
    header[0] = EDB_FLAG
    struct.pack_into("<I", header, 4, n_recs)
    struct.pack_into("<H", header, 8, REC_SIZE)
    struct.pack_into("<H", header, 10, table_size)
    records = b"".join(struct.pack("<i", value) for value in values)
    out = bytes(header) + records
    if len(out) < table_size:
        out += b"\xFF" * (table_size - len(out))
    return out


def build_v1_esp32(n_recs: int, values: list[int], table_size: int) -> bytes:
    header = bytearray(16)
    header[0] = EDB_FLAG
    struct.pack_into("<I", header, 4, n_recs)
    struct.pack_into("<H", header, 8, REC_SIZE)
    struct.pack_into("<I", header, 12, table_size)
    records = b"".join(struct.pack("<i", value) for value in values)
    out = bytes(header) + records
    if len(out) < table_size:
        out += b"\xFF" * (table_size - len(out))
    return out


def main() -> None:
    FIXTURES.mkdir(parents=True, exist_ok=True)
    avr = build_v1_avr(8, [1, 2, 3, 4, 5, 6, 7, 8], 128)
    esp32 = build_v1_esp32(4, [10, 20, 30, 40], 128)
    (FIXTURES / "v1_master_avr_8rec.db").write_bytes(avr)
    (FIXTURES / "v1_master_esp32_4rec.db").write_bytes(esp32)
    print(f"Wrote fixtures to {FIXTURES}")


if __name__ == "__main__":
    main()
