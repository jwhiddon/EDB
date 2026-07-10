#!/usr/bin/env python3
"""Validate an EDB v3 table in a file (headers, slot CRCs, counts, stable record_ids)."""

from __future__ import annotations

import argparse
import sys
from pathlib import Path

from edb_v3_io import check_table


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate EDB v3 on-disk tables.")
    parser.add_argument("input", type=Path, help="database file")
    parser.add_argument(
        "--offset",
        type=lambda x: int(x, 0),
        default=0,
        help="byte offset of table header (default 0)",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        help="treat warnings as errors (exit 1)",
    )
    args = parser.parse_args()

    data = args.input.read_bytes()
    issues, header = check_table(data, args.offset)

    if header is not None:
        print(
            f"table @ {args.offset}: v3 rec_size={header.rec_size} "
            f"n_slots={header.n_slots} n_live={header.n_live} "
            f"stable_ids={header.stable_ids()}"
        )

    if not issues:
        print("OK — no issues found")
        return 0

    errors = 0
    warns = 0
    for issue in issues:
        print(f"{issue.severity.upper()}: {issue.message}")
        if issue.severity == "error":
            errors += 1
        else:
            warns += 1

    if errors:
        return 1
    if args.strict and warns:
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
