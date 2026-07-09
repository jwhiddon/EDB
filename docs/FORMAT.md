# EDB On-Disk Format

All multi-byte integers are **little-endian**.

## v2 header (12 bytes)

Used by EDB 2.0.0 and later for all newly created databases.

| Offset | Size | Field | Value |
|--------|------|-------|-------|
| 0 | 1 | `flag` | `0xDB` |
| 1 | 1 | `version` | `2` |
| 2 | 4 | `n_recs` | `uint32` record count |
| 6 | 2 | `rec_size` | `uint16` bytes per record |
| 8 | 4 | `table_size` | `uint32` total bytes reserved for header + records |

Record data begins at `head_ptr + 12`.

Maximum records:

```
limit = (table_size - 12) / rec_size
```

### Example (v2)

A table with 2 records of 4 bytes each and `table_size = 32`:

```
Offset  Bytes (hex)   Meaning
0       DB            flag
1       02            version
2-5     02 00 00 00   n_recs = 2
6-7     04 00         rec_size = 4
8-11    20 00 00 00   table_size = 32
12-15   01 00 00 00   record 1
16-19   02 00 00 00   record 2
```

## v1 legacy headers

v1 used a C struct whose padding varied by platform. EDB 2.0.0 detects these layouts when opening:

### AVR-style (12-byte header)

| Offset | Field |
|--------|-------|
| 0 | `flag` |
| 4 | `n_recs` |
| 8 | `rec_size` |
| 10 | `table_size` (`uint16`) |

Record data begins at `head_ptr + 12`. The legacy AVR layout stores the low 16 bits of `table_size` at offset 10.

### ESP32-style (16-byte header)

| Offset | Field |
|--------|-------|
| 0 | `flag` |
| 4 | `n_recs` |
| 8 | `rec_size` |
| 12 | `table_size` |

Record data begins at `head_ptr + 16`.

Because v1 layouts differ, database files created on one MCU family are not reliably portable. Migrate to v2 for cross-platform use.

## Encryption extension (v2.1 optional)

When byte at `head_ptr + 12` is `0xE1` (`ext_magic`), an encryption descriptor follows before record data:

| Offset from `head_ptr + 12` | Size | Field |
|-----------------------------|------|-------|
| 0 | 1 | `ext_magic` = `0xE1` |
| 1 | 1 | `enc_version` (1 = chacha20_record_v1) |
| 2 | 1 | `enc_mode` (0 = e2e_blind, 1 = device_autonomous) |
| 3 | 1 | reserved |
| 4 | 16 | `salt` |
| 20 | 2 | `plaintext_rec_size` LE16 |
| 22 | 2 | `stored_rec_size` LE16 |
| 24 | 48 | `wrapped_table_key` (zero when `enc_mode` = blind; populated when autonomous) |

**Fixed extension size: 72 bytes** for both modes so record data always starts at `head_ptr + 84` (`12` v2 header + `72` extension). The same `.db` layout works for `e2e_blind` and `device_autonomous`; only `enc_mode` and the wrapped-key slot differ.

- `limit = (table_size - 84) / stored_rec_size` when encryption extension is present.
- v2 readers that do not check `ext_magic` treat offset 12 as record data (legacy behavior).

See [ENCRYPTION.md](ENCRYPTION.md).
