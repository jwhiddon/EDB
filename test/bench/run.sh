#!/usr/bin/env bash
# Build and run the EDB storage benchmark.
#
#   ./run.sh [N] [K]     e.g. ./run.sh 1024 16
#
# Compares, on the same logical workload:
#   * EDB 1.0.7 (release/1.0.7)          -- shift-based; byte-for-byte identical I/O to 1.0.6
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

N="${1:-1024}"
K="${2:-16}"
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

"$OUT/b107" "$N" "$K"
"$OUT/b200_nowid" "$N" "$K"
"$OUT/b200" "$N" "$K"
