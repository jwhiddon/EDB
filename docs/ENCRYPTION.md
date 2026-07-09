# EDB Encryption

EDB supports optional **three-layer** protection for gateway-managed databases:

1. **At-rest** — per-record authenticated encryption on device storage
2. **Transport** — session encryption over serial (or TCP in Phase 3)
3. **End-to-end** — gateway relays ciphertext only; browser decrypts with user passphrase

## Threat model

| Threat | Mitigation |
|--------|------------|
| SD card / EEPROM stolen | At-rest record encryption |
| USB serial sniffing | Transport session encryption |
| Compromised gateway / logs | E2E: no decryption keys on server |
| Lost passphrase | **No recovery** — back up wrapped keys |

**Non-goals:** HSM, secure enclave, multi-user ACLs, searchable encryption.

## At-rest: per-record AEAD

Each record slot stores `plaintext_len + 16` bytes (Poly1305 tag). Cipher: **XChaCha20-Poly1305** (default) or AES-128-GCM on ESP32 HW paths.

**Nonce** (12 bytes): derived from `table_id` (head_ptr) and **physical byte offset** of the slot — not logical `recno`. EDB shift operations move ciphertext without re-encryption.

```
nonce = truncate_12(SHA256(table_id LE32 || offset LE32))
```

## Key hierarchy

```
passphrase  →  Argon2id (browser)  →  master_key
master_key  →  HKDF-SHA256(salt, info=head_ptr)  →  table_key
table_key   →  XChaCha20-Poly1305 per record slot
```

- Passphrase never sent to device or gateway.
- Master key lives in browser session memory (Web Crypto).
- Device stores `salt` in header extension; optional wrapped table key for autonomous mode.

## Encryption modes

| Mode | Device keys | Use case |
|------|-------------|----------|
| `e2e_blind` | None | Manager CRUD; device stores opaque ciphertext |
| `device_autonomous` | KEK in NVS wraps table key | Firmware logs without host |

## Header extension (after v2 header)

Optional block at `head_ptr + 12` when `version == 2` and extension flag set:

| Offset | Size | Field |
|--------|------|-------|
| 0 | 1 | `ext_magic` = `0xE1` |
| 1 | 1 | `enc_version` (0=none, 1=chacha20_record_v1) |
| 2 | 1 | `enc_mode` (0=blind, 1=autonomous) |
| 3 | 1 | reserved |
| 4 | 16 | `salt` |
| 20 | 2 | `plaintext_rec_size` LE16 |
| 22 | 2 | `stored_rec_size` LE16 |
| 24 | 48 | `wrapped_table_key` (zeros in blind mode; AES-GCM wrap in autonomous) |

**Fixed 72-byte extension** for all encrypted tables. Record data always begins at `head_ptr + 84`, so one on-disk layout serves both `e2e_blind` and `device_autonomous` (upgrade path: flip `enc_mode` and write wrapped key without moving records).

v2 readers that do not understand extensions treat tables as standard v2 if `ext_magic != 0xE1`.

## Transport encryption

After `pair`:

- **ESP32 (Tier 1):** session key derived from pairing token via HKDF; lines encrypted with **ChaCha20-Poly1305** framing: `nonce(12) || ciphertext || tag(16)` then base64, or raw binary length prefix.
- **Mega (Tier 2):** AES-128-CTR + HMAC-PSK (lighter RAM).
- **Dev:** `EDB_GATEWAY_INSECURE=1` disables transport crypto on localhost.

Wire format inside encrypted envelope: same NDJSON as [GATEWAY.md](GATEWAY.md).

## RAM tiers

| Platform | Tier | At-rest | Transport | Autonomous |
|----------|------|---------|-----------|------------|
| ESP32 | 1 | Yes | Session crypto | Yes |
| Mega | 2 | Blind only | PSK-AES | No |
| Uno | 3 | Opaque bytes only | No bridge | No |

See [GATEWAY.md](GATEWAY.md) and [examples/EDB_SerialBridge/README.md](../examples/EDB_SerialBridge/README.md).

## Build flags

Optional compile-time flags (see [API.md](API.md) § Build flags):

| Flag | Default | Purpose |
|------|---------|---------|
| `EDB_ENABLE_CRYPTO` | off | Include `EDB_Crypto.h` |
| `EDB_CRYPTO_DEVICE_AUTONOMOUS` | off | On-device encrypt/decrypt + NVS |
| `EDB_CRYPTO_CIPHER_CHACHA20` | on if crypto | XChaCha20-Poly1305 |
| `EDB_CRYPTO_CIPHER_AES_GCM` | off | AES-128-GCM alternative |
| `EDB_NO_MALLOC_SHIFT` | off | Smaller shifts, lower peak RAM |
| `EDB_BRIDGE_ENABLE_TRANSPORT_CRYPTO` | on (ESP32) | Bridge serial session crypto |
| `EDB_BRIDGE_MAX_LINE` | 512 | Static RX buffer size |

## Test vectors

Shared vectors in `test/fixtures/crypto_vectors.json` — host native tests and Python gateway tests must match.

Example (table_id=0, offset=12, plaintext `01020304`, key=all `0x42`):

- See `crypto_vectors.json` for expected `payload_b64` after encrypt.

## Passphrase backup

If you lose the passphrase, encrypted data is **not recoverable**. Export wrapped keys from autonomous tables when rotating passphrases.
