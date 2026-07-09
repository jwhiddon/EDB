#!/usr/bin/env python3
"""Verify public API expectations for EDB release trees."""

from __future__ import annotations

import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

REQUIRED_METHODS = [
    "create",
    "open",
    "readRec",
    "deleteRec",
    "insertRec",
    "updateRec",
    "appendRec",
    "limit",
    "count",
    "clear",
]

REQUIRED_METHODS_V2 = [
    "headPtr",
    "tableSize",
    "nextTableOffset",
    "openOrCreate",
]

REQUIRED_TYPES = [
    "EDB_Write_Handler",
    "EDB_Read_Handler",
    "EDB_Write_Buffer",
    "EDB_Read_Buffer",
    "EDB_OK",
    "EDB_ERROR",
    "EDB_OUT_OF_RANGE",
    "EDB_TABLE_FULL",
]


def check_header(
    path: Path,
    expect_edb_extern: bool,
    expect_clear_status: bool,
    expect_v2_helpers: bool = False,
) -> list[str]:
    errors: list[str] = []
    text = path.read_text(encoding="utf-8")
    methods = REQUIRED_METHODS + (REQUIRED_METHODS_V2 if expect_v2_helpers else [])
    for method in methods:
        if method not in text:
            errors.append(f"{path}: missing symbol {method}")
    for typedef_name in REQUIRED_TYPES:
        if typedef_name not in text:
            errors.append(f"{path}: missing symbol {typedef_name}")
    if expect_edb_extern and "extern EDB edb" not in text:
        errors.append(f"{path}: expected extern EDB edb")
    if not expect_edb_extern and "extern EDB edb" in text and "EDB_NO_GLOBAL" not in text:
        errors.append(f"{path}: extern EDB edb should be optional in 2.0.0 (guarded by EDB_NO_GLOBAL)")
    if expect_clear_status:
        if "EDB_Status clear()" not in text:
            errors.append(f"{path}: expected EDB_Status clear()")
    else:
        if "void clear()" not in text:
            errors.append(f"{path}: expected void clear()")
    return errors


def main() -> int:
    errors: list[str] = []
    errors.extend(check_header(ROOT / "release" / "1.0.7" / "EDB.h", True, False, False))
    errors.extend(check_header(ROOT / "EDB.h", True, True, True))
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    print("API compatibility checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
