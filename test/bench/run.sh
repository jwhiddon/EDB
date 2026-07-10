#!/usr/bin/env bash
# Build and run the EDB storage benchmark.
#
#   ./run.sh            use the shipped test/data/sensorlog.bin (10,000 records)
#   ./run.sh 100000     regenerate a 100,000-record dataset in a temp dir and run against it
#                       (the shipped test/data files are never overwritten)
#
# Compares, on the same logical workload against identical record bytes:
#   * EDB 1.0.7 (release/1.0.7)                    -- shift-based
#   * EDB 2.0.0 with EDB_WRITE_IF_DIFFERENT=0
#   * EDB 2.0.0 with EDB_WRITE_IF_DIFFERENT=1 (default)
#
# See docs/BENCHMARK.md for recorded results and interpretation.
set -euo pipefail

CXX="${CXX:-g++}"
HERE="$(cd "$(dirname "$0")" && pwd)"
ROOT="$HERE/../.."
OUT="$(mktemp -d)"
trap 'rm -rf "$OUT"' EXIT

COUNT="${1:-}"
K="${2:-16}"

if [ -n "$COUNT" ]; then
  DATA="$OUT/data"
  python "$ROOT/tools/gen_datasets.py" --count "$COUNT" --out "$DATA"
else
  DATA="$ROOT/test/data"
fi

FLAGS="-std=c++11 -O2 -I$ROOT/test/support"

echo "building..."
"$CXX" $FLAGS -I"$ROOT/release/1.0.7" -DEDB_TEST \
  -DVERSION_NAME='"EDB 1.0.7 (shift-based)"' \
  "$HERE/bench.cpp" "$ROOT/release/1.0.7/EDB.cpp" -o "$OUT/b107"

"$CXX" $FLAGS -I"$ROOT" -DEDB_WRITE_IF_DIFFERENT=0 \
  -DVERSION_NAME='"EDB 2.0.0 (write-if-different OFF)"' \
  "$HERE/bench.cpp" "$ROOT/EDB.cpp" -o "$OUT/b200_nowid"

"$CXX" $FLAGS -I"$ROOT" \
  -DVERSION_NAME='"EDB 2.0.0 (write-if-different ON)"' \
  "$HERE/bench.cpp" "$ROOT/EDB.cpp" -o "$OUT/b200"

"$OUT/b107" "$DATA" "$K"
"$OUT/b200_nowid" "$DATA" "$K"
"$OUT/b200" "$DATA" "$K"
