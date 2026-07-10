import logging

import pytest
from edb_gateway.protocol import sanitize_for_log


def test_sanitize_payload(caplog):
    caplog.set_level(logging.DEBUG)
    logging.debug("evt %s", sanitize_for_log({"payload_b64": "AAAA", "id": 1}))
    assert "AAAA" not in caplog.text
    assert "redacted" in caplog.text
