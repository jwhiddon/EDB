#!/usr/bin/env python3
"""Generate a legacy EDB v1 database file for testing format conversion.

Produces a v1 (AVR or ESP32) file whose record contents are either seeded pseudo-random bytes
or copied from a raw file. tools/edb_migrate.py can then convert it to v2 or v3. Because the
generated data is deterministic for a given --seed, a test can regenerate the same records and
assert they survive the conversion byte-for-byte.

Examples:
    edb_make_v1.py legacy.db --arch avr --rec-size 22 --count 64
    edb_make_v1.py legacy.db --rec-size 22 --count 64 --from-raw test/data/sensorlog.bin
    edb_make_v1.py legacy.db --from-dataset test/data --arch esp32   # reads sensorlog.json
    edb_make_v1.py legacy.db --rec-size 4 --count 8 --table-size 128
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from edb_migrate import build_v1  # noqa: E402

DEFAULT_SEED = 0x1234ABCD
DEFAULT_REC_SIZE = 4
DEFAULT_COUNT = 8


def load_dataset(path: Path) -> tuple[dict, Path]:
    """Resolve a gen_datasets.py output. `path` is the sensorlog.json manifest or the directory
    containing it; returns (manifest, records_bin_path)."""
    if path.is_dir():
        path = path / "sensorlog.json"
    manifest = json.loads(path.read_text())
    bin_name = manifest["files"]["records"]["name"]
    return manifest, path.parent / bin_name


def verify_dataset_bin(manifest: dict, data: bytes) -> None:
    """Check the .bin against the manifest's recorded size and FNV-1a hash, so a stale or corrupt
    dataset is caught before it becomes a v1 file."""
    expected = manifest["files"]["records"]
    if len(data) != expected["bytes"]:
        raise ValueError(f"dataset .bin is {len(data)} bytes, manifest expects {expected['bytes']}")
    try:
        from gen_datasets import fnv1a64
    except ImportError:
        return  # generator not importable; size check already ran
    actual = "%016x" % fnv1a64(data)
    if actual != expected["fnv1a64"]:
        raise ValueError(f"dataset .bin hash {actual} != manifest {expected['fnv1a64']} (stale/corrupt)")


def gen_records(count: int, rec_size: int, seed: int) -> bytes:
    """Deterministic pseudo-random record bytes via xorshift32 (same seed -> same bytes)."""
    state = (seed & 0xFFFFFFFF) or 1
    out = bytearray(count * rec_size)
    for i in range(len(out)):
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        out[i] = state & 0xFF
    return bytes(out)


def make_v1(output: Path, arch: str, rec_size: int, count: int,
            table_size: int | None, seed: int, from_raw: Path | None, force: bool) -> int:
    if count <= 0:
        raise ValueError("--count must be greater than zero")
    if from_raw is not None:
        raw = from_raw.read_bytes()
        need = count * rec_size
        if len(raw) < need:
            raise ValueError(f"{from_raw} has {len(raw)} bytes, need {need} "
                             f"({count} x {rec_size})")
        records = raw[:need]
    else:
        records = gen_records(count, rec_size, seed)

    data = build_v1(arch, rec_size, records, table_size)

    if output.exists() and not force:
        raise FileExistsError(f"refusing to overwrite {output} without --force")
    tmp = output.with_name(output.name + ".tmp")
    tmp.write_bytes(data)
    tmp.replace(output)
    print(f"wrote v1 {arch} {output} "
          f"(records={count}, rec_size={rec_size}, bytes={len(data)})")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Generate a legacy EDB v1 file for testing conversion")
    parser.add_argument("output", type=Path, help="destination .db file")
    parser.add_argument("--arch", choices=("avr", "esp32"), default="avr",
                        help="v1 header layout (default: avr)")
    parser.add_argument("--rec-size", type=int, default=None,
                        help="bytes per record (default: 4, or record_size from --from-dataset)")
    parser.add_argument("--count", type=int, default=None,
                        help="number of records (default: 8, or count from --from-dataset)")
    parser.add_argument("--table-size", type=int, default=None,
                        help="total file size in bytes (default: tight fit; larger pads with 0xFF)")
    parser.add_argument("--seed", type=lambda s: int(s, 0), default=DEFAULT_SEED,
                        help="xorshift32 seed for generated data (default: 0x1234ABCD)")
    parser.add_argument("--from-raw", type=Path, default=None,
                        help="take record bytes from this file instead of generating them")
    parser.add_argument("--from-dataset", type=Path, default=None,
                        help="use a gen_datasets.py output: the sensorlog.json manifest or its "
                             "directory; auto-fills --rec-size and --count and verifies the .bin")
    parser.add_argument("--force", action="store_true", help="overwrite output file if it exists")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    try:
        rec_size, count, from_raw = args.rec_size, args.count, args.from_raw
        if args.from_dataset is not None:
            if from_raw is not None:
                raise ValueError("use either --from-raw or --from-dataset, not both")
            manifest, bin_path = load_dataset(args.from_dataset)
            verify_dataset_bin(manifest, bin_path.read_bytes())
            if rec_size is None:
                rec_size = manifest["record_size"]
            elif rec_size != manifest["record_size"]:
                raise ValueError(f"--rec-size {rec_size} conflicts with dataset "
                                 f"record_size {manifest['record_size']}")
            if count is None:
                count = manifest["count"]
            from_raw = bin_path  # make_v1 slices count*rec_size from the .bin
        if rec_size is None:
            rec_size = DEFAULT_REC_SIZE
        if count is None:
            count = DEFAULT_COUNT
        return make_v1(args.output, args.arch, rec_size, count,
                       args.table_size, args.seed, from_raw, args.force)
    except (ValueError, FileExistsError, OSError, KeyError) as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
