# EDB Encryption

Optional record-level encryption, opt-in via `EDB_ENABLE_CRYPTO`. The **core `EDB` library is
encryption-agnostic**: it stores encrypted records as opaque bytes and never holds a key. All
crypto lives in `EDB_Crypto` (the C reference implementation, used for tests and the optional
`device_autonomous` mode) and, for `e2e_blind` tables, in the browser/host that owns the key.

## What is implemented today

| Layer | Status |
|-------|--------|
| At-rest per-record AEAD (`EDB_Crypto`) | **Implemented** — RFC 8439 ChaCha20-Poly1305 |
| Transport (serial wire) encryption | **Not implemented** — pairing establishes a session token but the serial line is currently plaintext. Do not rely on it for confidentiality; keep the USB link physically trusted or add a secure channel. |
| Browser end-to-end encryption (manager UI) | **Not implemented** — the manager UI stores/shows record bytes as-is; wire your own encrypt/decrypt with `EDB_Crypto`-compatible framing before relying on E2E. |

The sections below marked *(planned)* are design intent, not shipped behavior.

## At-rest: per-record AEAD

Cipher: **ChaCha20-Poly1305 (RFC 8439)**, validated against the RFC test vector and OpenSSL.

A sealed record stores its own fresh nonce, so re-encrypting an updated record never reuses
keystream:

```
sealed record = nonce(12) || ciphertext(plaintext_len) || tag(16)
stored_rec_size = plaintext_len + 28          (EDB_CRYPTO_RECORD_OVERHEAD)
```

- **Nonce:** a fresh 96-bit value chosen per write by the caller (random in the browser, or a
  per-slot counter on device). It is stored with the record — never derived from the slot location.
- **Authentication / AAD:** the record identity `table_id (LE32) || record_id (LE32)` is
  authenticated as associated data. A ciphertext moved to a different table or slot fails
  verification, so records cannot be silently swapped or replayed.
- **Verification:** the tag is checked in constant time before decryption.

### Reference API (`EDB_Crypto.h`)

```c
edb_crypto_aead_encrypt(key, nonce, aad, aad_len, plaintext, pt_len, ciphertext, tag);
edb_crypto_aead_decrypt(key, nonce, aad, aad_len, ciphertext, ct_len, tag, plaintext);
edb_crypto_seal_record(key, table_id, record_id, nonce, plaintext, pt_len, out, &out_len);
edb_crypto_open_record(key, table_id, record_id, in, in_len, plaintext, &pt_len);
```

`seal_record`/`open_record` produce and consume the `nonce || ciphertext || tag` framing above.

## Key hierarchy *(caller responsibility)*

Keys are derived and held by whoever owns them (browser or device firmware), never by the core
library or the gateway relay:

```
passphrase  →  KDF (host/browser)  →  master_key  →  per-table key  →  seal_record()
```

The gateway is a **ciphertext relay** and holds no keys.

## Encryption modes

| Mode | Device keys | Use case |
|------|-------------|----------|
| `e2e_blind` | None | Host/browser owns the key; device stores opaque ciphertext |
| `device_autonomous` | KEK wraps a table key (ESP32 NVS) | Firmware encrypts/decrypts without a host |

## Table descriptor (metadata)

An optional 72-byte descriptor (`ext_magic = 0xE1`) records encryption parameters (mode, salt,
plaintext/stored sizes, and a wrapped key for autonomous mode). It is metadata for the host/gateway;
the core library treats records as opaque regardless. See `edb_crypto_parse_ext` /
`edb_crypto_write_ext_blind`.

## Build flags

| Flag | Default | Purpose |
|------|---------|---------|
| `EDB_ENABLE_CRYPTO` | off | Compile `EDB_Crypto` (ChaCha20-Poly1305) |
| `EDB_CRYPTO_DEVICE_AUTONOMOUS` | off | Wrapped-key extension helpers for autonomous mode |

## Test vectors

`test/fixtures/crypto_vectors.json` holds an authoritative `seal_record` known-answer
(`sealed_hex = nonce || ciphertext || tag`, AAD = `table_id || record_id`). The native C test
reproduces it byte-for-byte; any other implementation that checks the same file is cross-validated.

## Passphrase backup

If a passphrase/key is lost, encrypted data is **not recoverable**. Back up wrapped keys before
rotating passphrases on autonomous tables.
