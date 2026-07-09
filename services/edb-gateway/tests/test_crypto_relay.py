import importlib
import pkgutil


def test_gateway_has_no_decrypt_module():
    import edb_gateway

    for mod in pkgutil.walk_packages(edb_gateway.__path__, edb_gateway.__name__ + "."):
        if "decrypt" in mod.name.lower() and "encrypt" not in mod.name.lower():
            raise AssertionError(f"unexpected decrypt module: {mod.name}")


def test_relay_payload_unchanged():
    from edb_gateway.protocol import b64_decode, b64_encode

    raw = b"\x01\x02\x03\x04"
    encoded = b64_encode(raw)
    assert b64_decode(encoded) == raw
