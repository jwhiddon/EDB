#!/usr/bin/env python3
"""Convert legacy EDB v1 database files to packed v2 format."""

from __future__ import annotations

import argparse
import shutil
import struct
import sys
from dataclasses import dataclass
from pathlib import Path

EDB_FLAG = 0xDB
EDB_VERSION = 2
V2_HEADER_SIZE = 12
V2_HEADER_FMT = "<BBIHI"  # flag, version, n_recs, rec_size, table_size


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
    is_v2: bool


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def parse_v2(data: bytes) -> EdbHeader:
    flag, version, n_recs, rec_size, table_size = struct.unpack_from(V2_HEADER_FMT, data, 0)
    return EdbHeader(flag, version, n_recs, rec_size, table_size, V2_HEADER_SIZE, True)


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
    return EdbHeader(flag, 1, n_recs, rec_size, table_size, layout.header_size, False)


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
    layouts = V1_LAYOUTS if arch == "auto" else [layout for layout in V1_LAYOUTS if layout.name == arch]
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

    if data[1] == EDB_VERSION:
        if len(data) < V2_HEADER_SIZE:
            raise ValueError("truncated v2 header")
        header = parse_v2(data)
        validate_header(header, len(data))
        return header

    return detect_v1_layout(data, arch)


def pack_v2(header: EdbHeader) -> bytes:
    return struct.pack(V2_HEADER_FMT, EDB_FLAG, EDB_VERSION, header.n_recs, header.rec_size, header.table_size)


def migrate_bytes(data: bytes, arch: str) -> tuple[bytes, EdbHeader, bool]:
    header = read_header(data, arch)
    if header.is_v2:
        return data, header, False

    record_bytes = header.n_recs * header.rec_size
    records = data[header.header_size : header.header_size + record_bytes]
    if len(records) != record_bytes:
        raise ValueError("missing record bytes in source file")

    out = bytearray(pack_v2(header))
    out.extend(records)
    if header.table_size > len(out):
        out.extend(b"\xFF" * (header.table_size - len(out)))
    elif header.table_size < len(out):
        raise ValueError("computed v2 file larger than declared table size")

    return bytes(out), header, True


def migrate_file(input_path: Path, output_path: Path, arch: str, force: bool) -> int:
    data = input_path.read_bytes()
    output, header, changed = migrate_bytes(data, arch)

    if not changed:
        print(f"{input_path}: already v2 format")
        if input_path.resolve() != output_path.resolve():
            if output_path.exists() and not force:
                raise FileExistsError(f"refusing to overwrite {output_path} without --force")
            shutil.copyfile(input_path, output_path)
        return 0

    if output_path.exists() and not force:
        raise FileExistsError(f"refusing to overwrite {output_path} without --force")

    output_path.write_bytes(output)
    print(
        f"migrated {input_path} -> {output_path} "
        f"(records={header.n_recs}, rec_size={header.rec_size}, table_size={header.table_size})"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Migrate legacy EDB v1 files to packed v2 format")
    parser.add_argument("input", type=Path, help="source .db file")
    parser.add_argument("output", type=Path, help="destination .db file")
    parser.add_argument(
        "--arch",
        choices=("avr", "esp32", "auto"),
        default="auto",
        help="legacy v1 header layout (default: auto)",
    )
    parser.add_argument("--force", action="store_true", help="overwrite output file if it exists")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return migrate_file(args.input, args.output, args.arch, args.force)
    except (ValueError, FileExistsError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
