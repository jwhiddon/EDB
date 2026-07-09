import struct
from pathlib import Path

import pytest

from edb_gateway.edb_v3 import CORRUPT, DELETED, OK, HEADER_SPAN, EdbV3File

REPO_ROOT = Path(__file__).resolve().parents[3]
C_FIXTURE = REPO_ROOT / "test" / "fixtures" / "v3_migrated_avr_8rec.db"


def test_roundtrip(tmp_path):
    db = EdbV3File(str(tmp_path / "t.db"))
    assert db.create(0, 512, 4) == OK
    for i in range(1, 4):
        status, recno = db.append_rec(0, struct.pack("<i", i * 10))
        assert status == OK and recno == i
    assert db.count(0) == 3

    status, payload = db.read_rec(0, 2)
    assert status == OK and struct.unpack("<i", payload)[0] == 20

    assert db.delete_rec(0, 2) == OK
    assert db.count(0) == 2
    assert db.read_rec(0, 2)[0] == DELETED

    # freed slot 2 is reused by the free-list
    status, recno = db.append_rec(0, struct.pack("<i", 99))
    assert recno == 2

    # reopen from disk
    reopened = EdbV3File(str(tmp_path / "t.db"))
    assert reopened.count(0) == 3
    assert struct.unpack("<i", reopened.read_rec(0, 2)[1])[0] == 99


def test_corrupt_record_detected(tmp_path):
    db = EdbV3File(str(tmp_path / "c.db"))
    db.create(0, 512, 4)
    db.append_rec(0, struct.pack("<i", 5))
    db.data[HEADER_SPAN + 1] ^= 0xFF  # flip a payload byte of slot 0
    assert db.read_rec(0, 1)[0] == CORRUPT


@pytest.mark.skipif(not C_FIXTURE.exists(), reason="run tools/generate_fixtures.py first")
def test_reads_c_format_fixture():
    # A file produced by the C-compatible migration tool must open here with all records intact,
    # proving the Python and C v3 layouts agree (CRC32 header + CRC16 slots).
    db = EdbV3File(str(C_FIXTURE))
    assert db.open(0) == OK
    assert db.count(0) == 8
    values = [struct.unpack("<i", db.read_rec(0, r)[1])[0] for r in range(1, 9)]
    assert values == [1, 2, 3, 4, 5, 6, 7, 8]
