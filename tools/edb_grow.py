#!/usr/bin/env python3
"""Grow a fixed-size EDB v3 table on disk (host-side).

Arduino/EEPROM tables are created with a fixed table_size. When you need more record capacity,
grow the file on a PC, update any firmware size constants, then copy the file back to the device.

Does not modify record bytes or slot layout — only extends the reserved region and updates the
header table_size field. Subsequent tables in the same file are shifted and their head_ptr values
in firmware must be increased by the same delta.
"""

from __future__ import annotations

import argparse
import os
import sys
from pathlib import Path

from edb_v3_io import active_header, check_table, grow_table


def _atomic_write(path: Path, data: bytes) -> None:
    tmp = path.with_name(path.name + ".tmp")
    tmp.write_bytes(data)
    os.replace(tmp, path)


def _parse_size(value: str) -> int:
    return int(value, 0)


def grow_file(
    input_path: Path,
    output_path: Path,
    *,
    head_ptr: int,
    new_table_size: int | None,
    add_slots: int | None,
    limit_slots: int | None,
    force: bool,
    check: bool,
    dry_run: bool,
) -> int:
    data = input_path.read_bytes()

    if check:
        issues, _ = check_table(data, head_ptr)
        errors = [i for i in issues if i.severity == "error"]
        if errors:
            for e in errors:
                print(f"ERROR: {e.message}", file=sys.stderr)
            return 1

    active = active_header(data, head_ptr)
    if active is None:
        print(f"no valid v3 table at offset {head_ptr}", file=sys.stderr)
        return 1
    h_before, _ = active

    try:
        result = grow_table(
            data,
            head_ptr,
            new_table_size=new_table_size,
            add_slots=add_slots,
            limit_slots=limit_slots,
        )
    except ValueError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1

    old_limit = h_before.max_slots()
    new_limit = result.header.max_slots()
    delta = result.new_table_size - result.old_table_size

    print(
        f"table @ {head_ptr}: table_size {result.old_table_size} -> {result.new_table_size} "
        f"(+{delta} bytes)"
    )
    print(f"  limit(): {old_limit} -> {new_limit} slots")
    print(f"  n_slots={result.header.n_slots} n_live={result.header.n_live} (unchanged)")

    if result.shifted_tables:
        print("  following tables shifted — update head_ptr in firmware:")
        for old_off, new_off in result.shifted_tables:
            print(f"    {old_off} -> {new_off}")
    else:
        file_end = head_ptr + result.old_table_size
        if len(data) > file_end:
            print(
                f"  note: {len(data) - file_end} byte(s) after this table were shifted by +{delta}",
                file=sys.stderr,
            )

    print(
        "  reminder: increase the storage buffer / partition size in your sketch to at least "
        f"{result.new_table_size} bytes at head_ptr {head_ptr}"
    )

    if dry_run:
        print("(dry run — no file written)")
        return 0

    in_place = input_path.resolve() == output_path.resolve()
    if output_path.exists() and not in_place and not force:
        print(f"refusing to overwrite {output_path} without --force", file=sys.stderr)
        return 2

    _atomic_write(output_path, bytes(result.data))
    print(f"written {output_path}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        description="Grow the reserved size of an EDB v3 table (host-side).",
        epilog=(
            "Specify exactly one growth mode: --add-slots, --limit, or --table-size. "
            "Copy the output file to the device and update TABLE_SIZE / storage[] / "
            "downstream head_ptr constants in firmware."
        ),
    )
    parser.add_argument("input", type=Path, help="source .db file")
    parser.add_argument("output", type=Path, help="destination .db file (or same as input)")
    parser.add_argument(
        "--offset",
        type=_parse_size,
        default=0,
        help="byte offset of table header (default 0)",
    )
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument(
        "--add-slots",
        type=int,
        metavar="N",
        help="increase capacity by N additional record slots",
    )
    mode.add_argument(
        "--limit",
        type=int,
        metavar="N",
        help="set maximum slots to N (new limit())",
    )
    mode.add_argument(
        "--table-size",
        type=_parse_size,
        metavar="BYTES",
        help="set table_size to an absolute byte value",
    )
    parser.add_argument("--force", action="store_true", help="overwrite output file")
    parser.add_argument("--check", action="store_true", help="run edb_check before growing")
    parser.add_argument("--dry-run", action="store_true", help="print plan only, do not write")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    return grow_file(
        args.input,
        args.output,
        head_ptr=args.offset,
        new_table_size=args.table_size,
        add_slots=args.add_slots,
        limit_slots=args.limit,
        force=args.force,
        check=args.check,
        dry_run=args.dry_run,
    )


if __name__ == "__main__":
    raise SystemExit(main())
