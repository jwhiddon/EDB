#!/usr/bin/env python3
"""Convert legacy EDB v1 (AVR/ESP32) or v2 database files to the v3 format.

v3 uses a redundant, CRC32-checksummed 48-byte header (stored twice) and framed record
slots (status byte + CRC16). Migrated records are written as dense LIVE slots with stable
ids 1..N. Run this on a computer, then copy the output file back to the device's storage.
"""

from __future__ import annotations

import argparse
import os
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

EDB_FLAG = 0xDB
V1_VERSION = 1
V2_VERSION = 2
V3_VERSION = 3

# legacy v2 packed header (flag, version, n_recs, rec_size, table_size)
V2_HEADER_SIZE = 12
V2_HEADER_FMT = "<BBIHI"

# v3 layout constants (must match EDB.h / EDB.cpp)
V3_HEADER_COPY_SIZE = 48
V3_HEADER_SPAN = 2 * V3_HEADER_COPY_SIZE  # default data_offset
V3_SLOT_LIVE = 0xA5
V3_FREE_NONE = 0xFFFFFFFF


# ---------------------------------------------------------------- checksums (match EDB.cpp)

def crc32_edb(data: bytes) -> int:
    crc = 0xFFFFFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xEDB88320 if (crc & 1) else (crc >> 1)
    return crc ^ 0xFFFFFFFF


def crc16_ccitt_edb(data: bytes) -> int:
    crc = 0xFFFF
    for b in data:
        crc ^= (b << 8)
        crc &= 0xFFFF
        for _ in range(8):
            crc = ((crc << 1) ^ 0x1021) & 0xFFFF if (crc & 0x8000) else (crc << 1) & 0xFFFF
    return crc


# ---------------------------------------------------------------- source header parsing

@dataclass(frozen=True)
class V1Layout:
    name: str
    header_size: int
    n_recs_offset: int
    rec_size_offset: int
    table_size_offset: int
    table_size_width: int


V1_LAYOUTS = (
    V1Layout("avr", 12, 4, 8, 10, 2),
    V1Layout("esp32", 16, 4, 8, 12, 4),
)


@dataclass
class EdbHeader:
    flag: int
    version: int
    n_recs: int
    rec_size: int
    table_size: int
    header_size: int


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def parse_v2(data: bytes) -> EdbHeader:
    flag, version, n_recs, rec_size, table_size = struct.unpack_from(V2_HEADER_FMT, data, 0)
    return EdbHeader(flag, V2_VERSION, n_recs, rec_size, table_size, V2_HEADER_SIZE)


def parse_v1(data: bytes, layout: V1Layout) -> EdbHeader:
    if len(data) < layout.header_size:
        raise ValueError(f"file too small for {layout.name} v1 header")
    flag = data[0]
    n_recs = read_u32(data, layout.n_recs_offset)
    rec_size = read_u16(data, layout.rec_size_offset)
    if layout.table_size_width == 2:
        table_size = read_u16(data, layout.table_size_offset)
    else:
        table_size = read_u32(data, layout.table_size_offset)
    return EdbHeader(flag, V1_VERSION, n_recs, rec_size, table_size, layout.header_size)


def validate_header(header: EdbHeader, file_size: int) -> None:
    if header.flag != EDB_FLAG:
        raise ValueError("invalid EDB flag byte")
    if header.rec_size == 0:
        raise ValueError("record size must be greater than zero")
    if header.table_size < header.header_size:
        raise ValueError("table size smaller than header")
    payload = header.table_size - header.header_size
    if payload < header.rec_size:
        raise ValueError("table cannot hold even one record")
    limit = payload // header.rec_size
    if header.n_recs > limit:
        raise ValueError("record count exceeds table limit")
    used = header.header_size + header.n_recs * header.rec_size
    if used > file_size:
        raise ValueError("record data extends beyond file size")


def detect_v1_layout(data: bytes, arch: str) -> EdbHeader:
    layouts = V1_LAYOUTS if arch == "auto" else [l for l in V1_LAYOUTS if l.name == arch]
    if not layouts:
        raise ValueError(f"unknown architecture: {arch}")
    errors: list[str] = []
    for layout in layouts:
        try:
            header = parse_v1(data, layout)
            validate_header(header, len(data))
            return header
        except ValueError as exc:
            errors.append(f"{layout.name}: {exc}")
    raise ValueError("could not parse v1 header; " + "; ".join(errors))


def read_header(data: bytes, arch: str) -> EdbHeader:
    if len(data) < 2:
        raise ValueError("file too small to be an EDB database")
    if data[0] != EDB_FLAG:
        raise ValueError("invalid EDB flag byte")
    if data[1] == V3_VERSION:
        return EdbHeader(data[0], V3_VERSION, 0, 0, 0, V3_HEADER_COPY_SIZE)
    if data[1] == V2_VERSION:
        if len(data) < V2_HEADER_SIZE:
            raise ValueError("truncated v2 header")
        header = parse_v2(data)
        validate_header(header, len(data))
        return header
    return detect_v1_layout(data, arch)


# ---------------------------------------------------------------- v3 building

def build_v3_header_copy(seq: int, n_slots: int, n_live: int, rec_size: int,
                         slot_stride: int, table_size: int,
                         free_head: int = V3_FREE_NONE,
                         data_offset: int = V3_HEADER_SPAN, flags: int = 0) -> bytes:
    h = bytearray(V3_HEADER_COPY_SIZE)
    h[0] = EDB_FLAG
    h[1] = V3_VERSION
    struct.pack_into("<H", h, 2, flags)
    struct.pack_into("<I", h, 4, seq)
    struct.pack_into("<I", h, 8, n_slots)
    struct.pack_into("<I", h, 12, n_live)
    struct.pack_into("<H", h, 16, rec_size)
    struct.pack_into("<H", h, 18, slot_stride)
    struct.pack_into("<I", h, 20, table_size)
    struct.pack_into("<I", h, 24, free_head)
    struct.pack_into("<H", h, 28, data_offset)
    # bytes 30..43 reserved (zero)
    struct.pack_into("<I", h, 44, crc32_edb(bytes(h[0:44])))
    return bytes(h)


def build_v3_slot(payload: bytes) -> bytes:
    status = bytes([V3_SLOT_LIVE])
    crc = crc16_ccitt_edb(status + payload)
    return status + payload + struct.pack("<H", crc)


def migrate_bytes(data: bytes, arch: str, table_size: int | None = None) -> tuple[bytes, EdbHeader, bool]:
    header = read_header(data, arch)
    if header.version == V3_VERSION:
        return data, header, False

    rec_size = header.rec_size
    n_recs = header.n_recs
    record_bytes = n_recs * rec_size
    records = data[header.header_size : header.header_size + record_bytes]
    if len(records) != record_bytes:
        raise ValueError("missing record bytes in source file")

    slot_stride = 1 + rec_size + 2
    data_offset = V3_HEADER_SPAN
    # Preserve the original record capacity unless the caller overrides table_size.
    src_capacity = (header.table_size - header.header_size) // rec_size
    capacity = max(src_capacity, n_recs)
    new_table_size = data_offset + capacity * slot_stride
    if table_size is not None:
        if table_size < data_offset + n_recs * slot_stride:
            raise ValueError("requested --table-size too small for the existing records")
        new_table_size = table_size

    copy = build_v3_header_copy(1, n_recs, n_recs, rec_size, slot_stride, new_table_size)
    out = bytearray(copy + copy)  # two identical copies (seq=1); copy 0 is active
    for i in range(n_recs):
        out.extend(build_v3_slot(records[i * rec_size : (i + 1) * rec_size]))
    if len(out) < new_table_size:
        out.extend(b"\x00" * (new_table_size - len(out)))
    elif len(out) > new_table_size:
        raise ValueError("computed v3 file larger than table size")
    return bytes(out), header, True


def _atomic_write(output_path: Path, data: bytes) -> None:
    tmp = output_path.with_name(output_path.name + ".tmp")
    tmp.write_bytes(data)
    os.replace(tmp, output_path)


def migrate_file(input_path: Path, output_path: Path, arch: str, force: bool,
                 table_size: int | None = None) -> int:
    data = input_path.read_bytes()
    output, header, changed = migrate_bytes(data, arch, table_size)
    in_place = input_path.resolve() == output_path.resolve()

    if not changed:
        print(f"{input_path}: already v3 format")
        if not in_place:
            if output_path.exists() and not force:
                raise FileExistsError(f"refusing to overwrite {output_path} without --force")
            _atomic_write(output_path, data)
        return 0

    if output_path.exists() and not in_place and not force:
        raise FileExistsError(f"refusing to overwrite {output_path} without --force")

    _atomic_write(output_path, output)
    print(
        f"migrated v{header.version} {input_path} -> v3 {output_path} "
        f"(records={header.n_recs}, rec_size={header.rec_size})"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Migrate legacy EDB v1/v2 files to the v3 format")
    parser.add_argument("input", type=Path, help="source .db file")
    parser.add_argument("output", type=Path, help="destination .db file")
    parser.add_argument("--arch", choices=("avr", "esp32", "auto"), default="auto",
                        help="legacy v1 header layout (default: auto)")
    parser.add_argument("--table-size", type=int, default=None,
                        help="override the v3 table_size in bytes (default: preserve capacity)")
    parser.add_argument("--force", action="store_true", help="overwrite output file if it exists")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return migrate_file(args.input, args.output, args.arch, args.force, args.table_size)
    except (ValueError, FileExistsError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
