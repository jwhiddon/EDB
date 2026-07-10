import json

import pytest
from edb_gateway.models import EdbStatus, ProtocolMessage, ProtocolResponse
from edb_gateway.protocol import encode_line, parse_line, sanitize_for_log


def test_encode_line():
    msg = ProtocolMessage(id=1, cmd="ping")
    line = encode_line(msg)
    assert json.loads(line)["cmd"] == "ping"


def test_parse_response():
    resp = parse_line('{"id":2,"status":"EDB_OK","data":{"count":3}}')
    assert resp.id == 2
    assert resp.status == EdbStatus.OK
    assert resp.data["count"] == 3


def test_sanitize_redacts_payload():
    log = sanitize_for_log({"payload_b64": "secret", "cmd": "readRec"})
    assert log["payload_b64"] == "<redacted>"
    assert log["cmd"] == "readRec"
