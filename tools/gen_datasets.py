#!/usr/bin/env python3
"""Generate deterministic, shippable test datasets for EDB.

Produces a realistic sensor-log dataset as fixed-layout binary records plus a JSON manifest that
documents the schema, counts, per-field ranges, a few decoded sample records, and an FNV-1a hash.
Because the data is generated from a fixed seed by this script, the expected contents are known and
verifiable: the native benchmark and tests consume the .bin, and tools/test_datasets.py checks that
the shipped files still match a fresh regeneration.

Record layout (packed, little-endian, 22 bytes) -- matches SensorRec in test/bench/bench.cpp:

    offset  size  field        type    meaning
    0       4     id           uint32  logical record id (1000 + index)
    4       4     ts           uint32  unix seconds, mostly increasing
    8       2     temp_c100    int16   temperature, centi-degrees C
    10      2     humidity     uint16  relative humidity, centi-percent
    12      1     status       uint8   flag bits
    13      1     channel      uint8   sensor channel 0..7
    14      8     tag          char[8] fixed-width location name, NUL-padded
"""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

REC_FMT = "<IIhHBB8s"
REC_SIZE = struct.calcsize(REC_FMT)   # 22
assert REC_SIZE == 22, REC_SIZE

TAGS = ["kitchen", "garage", "attic", "cellar", "porch", "shed", "office", "lab"]

# Seeds are part of the contract: changing them changes the shipped files (and their hashes).
SEED_BASE = 0x1234ABCD
SEED_UPDATE = 0x77AA55FF


def _xorshift32(seed: int):
    state = seed & 0xFFFFFFFF

    def nxt() -> int:
        nonlocal state
        state ^= (state << 13) & 0xFFFFFFFF
        state ^= state >> 17
        state ^= (state << 5) & 0xFFFFFFFF
        return state

    return nxt


def _tag(i: int) -> bytes:
    return TAGS[i % 8].encode("ascii")[:8].ljust(8, b"\x00")


def gen_records(n: int, seed: int) -> list[tuple]:
    rnd = _xorshift32(seed)
    out = []
    for i in range(n):
        rid = 1000 + i
        ts = 1700000000 + i * 10 + (rnd() % 7)
        temp = 500 + (rnd() % 2000)          # 5.00 .. 24.99 C
        humidity = 3000 + (rnd() % 5000)     # 30.00 .. 79.99 %
        status = rnd() & 0x0F
        channel = i % 8
        out.append((rid, ts, temp, humidity, status, channel, _tag(i)))
    return out


def gen_updates(base: list[tuple], seed: int) -> list[tuple]:
    """A later reading from each sensor: same id/channel/tag, new ts/temp/humidity/status."""
    rnd = _xorshift32(seed)
    out = []
    for (rid, ts, _temp, _hum, _status, channel, tag) in base:
        out.append((rid,
                    ts + 3600 + (rnd() % 60),
                    500 + (rnd() % 2000),
                    3000 + (rnd() % 5000),
                    rnd() & 0x0F,
                    channel,
                    tag))
    return out


def pack(records: list[tuple]) -> bytes:
    return b"".join(struct.pack(REC_FMT, *r) for r in records)


def fnv1a64(data: bytes) -> int:
    h = 0xCBF29CE484222325
    for b in data:
        h ^= b
        h = (h * 0x100000001B3) & 0xFFFFFFFFFFFFFFFF
    return h


def _decode(rec: tuple) -> dict:
    rid, ts, temp, hum, status, channel, tag = rec
    return {
        "id": rid, "ts": ts, "temp_c100": temp, "humidity": hum,
        "status": status, "channel": channel,
        "tag": tag.rstrip(b"\x00").decode("ascii"),
    }


def write_dataset(out_dir: Path, count: int) -> dict:
    out_dir.mkdir(parents=True, exist_ok=True)
    base = gen_records(count, SEED_BASE)
    upd = gen_updates(base, SEED_UPDATE)
    base_bytes = pack(base)
    upd_bytes = pack(upd)

    (out_dir / "sensorlog.bin").write_bytes(base_bytes)
    (out_dir / "sensorlog_updates.bin").write_bytes(upd_bytes)

    manifest = {
        "description": "EDB test dataset: realistic sensor log, deterministic from a fixed seed",
        "record_format": REC_FMT,
        "record_size": REC_SIZE,
        "count": count,
        "fields": [
            {"name": "id", "type": "uint32", "offset": 0},
            {"name": "ts", "type": "uint32", "offset": 4},
            {"name": "temp_c100", "type": "int16", "offset": 8},
            {"name": "humidity", "type": "uint16", "offset": 10},
            {"name": "status", "type": "uint8", "offset": 12},
            {"name": "channel", "type": "uint8", "offset": 13},
            {"name": "tag", "type": "char[8]", "offset": 14},
        ],
        "tags": TAGS,
        "seed_base": SEED_BASE,
        "seed_update": SEED_UPDATE,
        "files": {
            "records": {"name": "sensorlog.bin", "bytes": len(base_bytes),
                        "fnv1a64": "%016x" % fnv1a64(base_bytes)},
            "updates": {"name": "sensorlog_updates.bin", "bytes": len(upd_bytes),
                        "fnv1a64": "%016x" % fnv1a64(upd_bytes)},
        },
        "samples": {
            "records[0]": _decode(base[0]),
            "records[1]": _decode(base[1]),
            "records[last]": _decode(base[-1]),
            "updates[0]": _decode(upd[0]),
        },
    }
    (out_dir / "sensorlog.json").write_text(json.dumps(manifest, indent=2) + "\n")
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Generate EDB test datasets")
    parser.add_argument("--count", type=int, default=10000, help="number of records (default 10000)")
    parser.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[1] / "test" / "data")
    args = parser.parse_args(argv)
    m = write_dataset(args.out, args.count)
    print(f"wrote {args.count} records to {args.out}")
    print(f"  sensorlog.bin         fnv1a64={m['files']['records']['fnv1a64']} ({m['files']['records']['bytes']} bytes)")
    print(f"  sensorlog_updates.bin fnv1a64={m['files']['updates']['fnv1a64']} ({m['files']['updates']['bytes']} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
