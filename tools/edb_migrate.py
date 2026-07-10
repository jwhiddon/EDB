#!/usr/bin/env python3
"""Convert legacy EDB database files between on-disk formats.

Reads a v1 (AVR/ESP32) or v2 file and writes a v2 or v3 file (choose with --to; default v3).

v3 uses a redundant, CRC32-checksummed 48-byte header (stored twice) and framed record
slots (status byte + CRC16). Migrated records are written as dense LIVE slots with stable
ids 1..N. v2 is the legacy packed format: a single 12-byte header and flat, unframed records.

Converting v1 -> v2 is a header rewrite only (record bytes are copied verbatim), useful when
targeting older firmware that still reads v2. v3 is the recommended target for its crash-safety
and per-record integrity. Downgrading v3 -> v2/v1 is intentionally not supported (it would drop
integrity metadata and reintroduce the defects v3 fixes).

Run this on a computer, then copy the output file back to the device's storage.
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


def build_v1(arch: str, rec_size: int, records: bytes,
             table_size: int | None = None) -> bytes:
    """Assemble a legacy v1 file (the exact inverse of parse_v1). Useful for producing test
    inputs for the converter. `records` is the flat record region; its length must be a whole
    number of rec_size records. table_size defaults to a tight fit; a larger value pads with
    0xFF (the erased-storage convention)."""
    layouts = {layout.name: layout for layout in V1_LAYOUTS}
    if arch not in layouts:
        raise ValueError(f"unknown architecture: {arch} (expected 'avr' or 'esp32')")
    layout = layouts[arch]
    n_recs, remainder = divmod(len(records), rec_size)
    if rec_size == 0:
        raise ValueError("record size must be greater than zero")
    if remainder != 0:
        raise ValueError("record bytes are not a whole number of records")
    tight = layout.header_size + len(records)
    if table_size is None:
        table_size = tight
    elif table_size < tight:
        raise ValueError("requested table-size too small for the given records")
    if layout.table_size_width == 2 and table_size > 0xFFFF:
        raise ValueError("table-size exceeds the 16-bit avr field; use --arch esp32")

    header = bytearray(layout.header_size)
    header[0] = EDB_FLAG
    struct.pack_into("<I", header, layout.n_recs_offset, n_recs)
    struct.pack_into("<H", header, layout.rec_size_offset, rec_size)
    if layout.table_size_width == 2:
        struct.pack_into("<H", header, layout.table_size_offset, table_size)
    else:
        struct.pack_into("<I", header, layout.table_size_offset, table_size)
    out = bytearray(header)
    out.extend(records)
    if len(out) < table_size:
        out.extend(b"\xFF" * (table_size - len(out)))
    return bytes(out)


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


def build_v2(n_recs: int, rec_size: int, table_size: int, records: bytes) -> bytes:
    header = struct.pack(V2_HEADER_FMT, EDB_FLAG, V2_VERSION, n_recs, rec_size, table_size)
    out = bytearray(header)
    out.extend(records)
    if len(out) < table_size:
        out.extend(b"\xFF" * (table_size - len(out)))  # 0xFF = erased-storage convention (matches v1)
    elif len(out) > table_size:
        raise ValueError("computed v2 file larger than table size")
    return bytes(out)


def _extract_records(data: bytes, header: EdbHeader) -> bytes:
    record_bytes = header.n_recs * header.rec_size
    records = data[header.header_size : header.header_size + record_bytes]
    if len(records) != record_bytes:
        raise ValueError("missing record bytes in source file")
    return records


def _migrate_to_v2(data: bytes, header: EdbHeader,
                   table_size: int | None) -> tuple[bytes, EdbHeader, bool]:
    if header.version == V2_VERSION:
        return data, header, False
    if header.version == V3_VERSION:
        raise ValueError("downgrade from v3 to v2 is not supported")

    rec_size = header.rec_size
    n_recs = header.n_recs
    records = _extract_records(data, header)

    # Preserve the original record capacity unless the caller overrides table_size.
    src_capacity = (header.table_size - header.header_size) // rec_size
    capacity = max(src_capacity, n_recs)
    new_table_size = V2_HEADER_SIZE + capacity * rec_size
    if table_size is not None:
        if table_size < V2_HEADER_SIZE + n_recs * rec_size:
            raise ValueError("requested --table-size too small for the existing records")
        new_table_size = table_size

    return build_v2(n_recs, rec_size, new_table_size, records), header, True


def migrate_bytes(data: bytes, arch: str, table_size: int | None = None,
                  target: str = "v3") -> tuple[bytes, EdbHeader, bool]:
    if target not in ("v2", "v3"):
        raise ValueError(f"unknown target format: {target!r} (expected 'v2' or 'v3')")
    header = read_header(data, arch)
    if target == "v2":
        return _migrate_to_v2(data, header, table_size)

    if header.version == V3_VERSION:
        return data, header, False

    rec_size = header.rec_size
    n_recs = header.n_recs
    records = _extract_records(data, header)

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
                 table_size: int | None = None, target: str = "v3") -> int:
    data = input_path.read_bytes()
    output, header, changed = migrate_bytes(data, arch, table_size, target)
    in_place = input_path.resolve() == output_path.resolve()

    if not changed:
        print(f"{input_path}: already {target} format")
        if not in_place:
            if output_path.exists() and not force:
                raise FileExistsError(f"refusing to overwrite {output_path} without --force")
            _atomic_write(output_path, data)
        return 0

    if output_path.exists() and not in_place and not force:
        raise FileExistsError(f"refusing to overwrite {output_path} without --force")

    _atomic_write(output_path, output)
    print(
        f"migrated v{header.version} {input_path} -> {target} {output_path} "
        f"(records={header.n_recs}, rec_size={header.rec_size})"
    )
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Convert legacy EDB v1/v2 files to v2 or v3")
    parser.add_argument("input", type=Path, help="source .db file")
    parser.add_argument("output", type=Path, help="destination .db file")
    parser.add_argument("--to", dest="target", choices=("v2", "v3"), default="v3",
                        help="destination format (default: v3)")
    parser.add_argument("--arch", choices=("avr", "esp32", "auto"), default="auto",
                        help="legacy v1 header layout (default: auto)")
    parser.add_argument("--table-size", type=int, default=None,
                        help="override the output table_size in bytes (default: preserve capacity)")
    parser.add_argument("--force", action="store_true", help="overwrite output file if it exists")
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        return migrate_file(args.input, args.output, args.arch, args.force,
                            args.table_size, args.target)
    except (ValueError, FileExistsError, OSError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
