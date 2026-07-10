#!/usr/bin/env python3
"""Physically repack live EDB v3 slots (vacuum) and optionally emit a recno remap file.

Stable record_id values in payload[0..3] (when the table uses EDB_HDR_STABLE_IDS) are
preserved; only physical recno (slot index) may change. Clients that stored recno should
apply the remap JSON or switch to record_id / findRecById().
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

from edb_v3_io import RemapEntry, active_header, check_table, vacuum_table


def main() -> int:
    parser = argparse.ArgumentParser(description="Vacuum (repack) an EDB v3 table.")
    parser.add_argument("input", type=Path, help="source database file")
    parser.add_argument("output", type=Path, help="output database file")
    parser.add_argument(
        "--offset",
        type=lambda x: int(x, 0),
        default=0,
        help="byte offset of table header (default 0)",
    )
    parser.add_argument(
        "--remap-json",
        type=Path,
        help="write recno remap as JSON array of {record_id, old_recno, new_recno}",
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="overwrite existing output file",
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="run edb_check on input before vacuum; abort on errors",
    )
    args = parser.parse_args()

    if args.output.exists() and not args.force:
        print(f"refusing to overwrite {args.output} (use --force)", file=sys.stderr)
        return 2

    data = args.input.read_bytes()
    if args.check:
        issues, _ = check_table(data, args.offset)
        errors = [i for i in issues if i.severity == "error"]
        if errors:
            for e in errors:
                print(f"ERROR: {e.message}", file=sys.stderr)
            return 1

    before = active_header(data, args.offset)
    if before is None:
        print("no valid v3 header at offset", args.offset, file=sys.stderr)
        return 1
    h_before, _ = before
    live_before = h_before.n_live
    slots_before = h_before.n_slots

    out_data, remaps, h_after = vacuum_table(data, args.offset)
    if h_after is None:
        print("vacuum failed", file=sys.stderr)
        return 1

    args.output.write_bytes(out_data)

    print(
        f"vacuumed {args.input} -> {args.output}: "
        f"slots {slots_before} -> {h_after.n_slots}, live {live_before}"
    )
    if remaps:
        print(f"remapped {len(remaps)} record(s) — physical recno changed")
        for r in remaps:
            id_part = f" record_id={r.record_id}" if r.record_id else ""
            print(f"  recno {r.old_recno} -> {r.new_recno}{id_part}")
    else:
        print("no recno changes (already dense)")

    if args.remap_json:
        payload = [
            {
                "record_id": r.record_id,
                "old_recno": r.old_recno,
                "new_recno": r.new_recno,
            }
            for r in remaps
        ]
        args.remap_json.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
        print(f"remap written to {args.remap_json}")

    return 0


if __name__ == "__main__":
    sys.exit(main())
