#!/usr/bin/env bash
set -euo pipefail

make -C test fixtures
make -C test test
make -C test test-107
python -m unittest discover -s tools -p "test_*.py"
python scripts/check_api.py
