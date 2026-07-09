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
