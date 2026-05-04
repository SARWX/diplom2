# Measurement Table Binary Format

Version 1. All multi-byte fields are **little-endian**.

## Packet layout

```
Offset  Size  Type    Field
------  ----  ------  ------------------------------------------
0       1     uint8   magic[0]   = 0xAA
1       1     uint8   magic[1]   = 0xBB
2       1     uint8   version    = 1
3       1     uint8   row_count  — number of measurement rows
4 + 6*i 1     uint8   from_seq_id — source device sequential ID
5 + 6*i 1     uint8   to_seq_id   — target device sequential ID
6 + 6*i 4     int32   distance_mm — signed distance in millimetres
```

Total size: `4 + row_count * 6` bytes.

## Notes

- `distance_mm` is a signed 32-bit integer. Negative values are valid
  and indicate a measurement artefact (antenna delay not yet calibrated).
- `from_seq_id` and `to_seq_id` are assigned by the master during
  enumeration. seq_id 1 is always the main anchor.
- A packet with `row_count = 0` is a valid empty response (no
  measurements were obtained).
- The magic bytes allow the host to re-sync if partial data arrives.

## Example

Three devices (seq_ids 1, 2, 3). Device 2 measured distances to 1 and 3:

```
AA BB 01 02
02 01 E8 03 00 00    -- from=2, to=1, 1000 mm
02 03 D0 07 00 00    -- from=2, to=3, 2000 mm
```
