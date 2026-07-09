# EDB Encryption

Optional record-level encryption, opt-in via `EDB_ENABLE_CRYPTO`. The **core `EDB` library is
encryption-agnostic**: it stores encrypted records as opaque bytes and never holds a key. All
crypto lives in `EDB_Crypto` (the C reference implementation, used for tests and the optional
`device_autonomous` mode) and, for `e2e_blind` tables, in the browser/host that owns the key.

## What is implemented today

| Layer | Status |
|-------|--------|
| At-rest per-record AEAD (`EDB_Crypto`) | **Implemented** — RFC 8439 ChaCha20-Poly1305 |
| Transport (serial wire) encryption | **Implemented** — PSK session, ChaCha20-Poly1305 line framing (see below) |
| Browser end-to-end encryption (manager UI) | **Implemented** — WebCrypto AES-GCM per record (see below) |
| On-device autonomous at-rest encryption (bridge) | **Implemented** — opt-in; device seals/opens records with a provisioned key (see below) |

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

## Transport (serial wire) encryption

Protects the USB-serial link between the gateway and the Serial Bridge against passive sniffing and
unauthorized hosts, using a **pre-shared key (PSK)** provisioned on both sides (device: `EDB_BRIDGE_PSK`
in `config.h`; gateway: `EDB_GATEWAY_PSK`). Requires the `-DEDB_ENABLE_CRYPTO` build flag on the
bridge; without it the bridge runs in plaintext.

Handshake (nonces are public, sent in the clear):

```
host -> device : {"cmd":"pair","hnonce": <random 12B>}
device -> host : {"status":"EDB_OK","data":{"dnonce": <random 12B>, "confirm": <16B>}}
session_key = ChaCha20(psk, hnonce, ctr=1)[0..31] XOR ChaCha20(psk, dnonce, ctr=1)[0..31]
confirm     = ChaCha20(session_key, zero_nonce, ctr=1)[0..15]   (proves both sides share the PSK)
```

After pairing, every command and response line is ChaCha20-Poly1305 framed:

```
line = {"enc": base64( nonce(12) || ciphertext || tag(16) )}
nonce = direction(1) || counter(LE64) || 0x000000      dir 0x01 host->device, 0x02 device->host
```

Per-direction monotonic counters prevent replay/reordering. A wrong or missing PSK fails the
`confirm` check and pairing is refused. The session key is derived with a ChaCha20 PRF (no SHA
needed on the MCU); it is cross-validated by a shared known-answer test in the native C suite and
the gateway tests.

## Browser end-to-end encryption (manager UI)

The manager encrypts records **in the browser** with WebCrypto AES-256-GCM before they leave the
page, so the gateway and device only ever store ciphertext (`e2e_blind`). Unlock with a passphrase;
records then encrypt on append and decrypt on display.

- **Key:** `PBKDF2-HMAC-SHA256(passphrase, salt = SHA-256("edb-e2e-v1:" + head_ptr), 250000)` →
  AES-256-GCM. The salt is derived from the table's `head_ptr` (deterministic, not per-install
  random), so the same passphrase decrypts a table on any machine; **passphrase strength is the
  primary defense** — use a strong one.
- **Record layout:** `iv(12) || ciphertext || tag(16)`, base64 in `payload_b64`, so the table's
  `rec_size` must be `plaintext_len + 28`.
- **AAD:** `"edb-table:" + head_ptr` binds ciphertext to its table (cross-table reuse fails). Note:
  records are not individually bound to a slot id, so this does not authenticate reordering *within*
  a table; the device's per-record CRC still detects byte tampering.

## On-device autonomous at-rest encryption (bridge)

Opt-in via `EDB_BRIDGE_ENABLE_AT_REST_CRYPTO` (plus the `-DEDB_ENABLE_CRYPTO` build flag). When
enabled, the Serial Bridge **seals records on write and opens them on read** with a device key
(`EDB_BRIDGE_AT_REST_KEY` in `config.h` — provision it from ESP32 NVS in production, not a
compile-time constant). Plaintext never reaches storage, and the host sends/receives plaintext
payloads (the device does the crypto).

- Uses `edb_crypto_seal_record` / `edb_crypto_open_record`: `nonce(12) || ciphertext || tag(16)`,
  so tables must be created with `rec_size = plaintext_len + 28`.
- A **fresh random nonce** is generated per write (`esp_random` on ESP32), avoiding the nonce-reuse
  risk a reboot-resettable counter would have.
- AAD binds records to the table (`table_id = head_ptr`). This mode is mutually exclusive with
  `e2e_blind`: choose device-side crypto *or* browser E2E for a given table, not both.

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
