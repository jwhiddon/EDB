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


def test_crypto_vector_matches_openssl():
    # Independently verify the shared at-rest vector (test/fixtures/crypto_vectors.json) with an
    # OpenSSL-backed ChaCha20-Poly1305. The C++ device test drives seal_record from the same file,
    # so passing here establishes C++ <-> OpenSSL agreement on the sealed record format.
    import json
    import struct
    from pathlib import Path

    import pytest

    aead = pytest.importorskip("cryptography.hazmat.primitives.ciphers.aead")
    ChaCha20Poly1305 = aead.ChaCha20Poly1305

    vec_path = Path(__file__).resolve().parents[3] / "test" / "fixtures" / "crypto_vectors.json"
    if not vec_path.exists():
        pytest.skip("crypto_vectors.json fixture not present")
    v = json.loads(vec_path.read_text())

    key = bytes.fromhex(v["key_hex"])
    nonce = bytes.fromhex(v["nonce_hex"])
    plaintext = bytes.fromhex(v["plaintext_hex"])
    sealed = bytes.fromhex(v["sealed_hex"])
    aad = struct.pack("<II", v["table_id"], v["record_id"])  # table_id LE32 || record_id LE32

    # sealed = nonce(12) || ciphertext || tag(16); ChaCha20Poly1305.encrypt returns ciphertext||tag.
    assert sealed[:12] == nonce
    assert sealed[12:] == ChaCha20Poly1305(key).encrypt(nonce, plaintext, aad)
