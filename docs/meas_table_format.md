# Measurement Table Binary Format

Version 2. All multi-byte fields are **little-endian**.

## Packet layout

```
Offset   Size  Type    Field
-------  ----  ------  ------------------------------------------
0        1     uint8   magic[0]   = 0xAA
1        1     uint8   magic[1]   = 0xBB
2        1     uint8   version    = 2
3        1     uint8   row_count  — number of measurement rows
4 + 7*i  1     uint8   from_seq_id    — source device sequential ID
5 + 7*i  1     uint8   to_seq_id      — target device sequential ID
6 + 7*i  4     int32   distance_mm    — signed distance in millimetres
10 + 7*i 1     int8    temperature_c  — temperature at source device (°C)
```

Total size: `4 + row_count * 7` bytes.

## Notes

- `distance_mm` is a signed 32-bit integer. Negative values are valid and
  indicate an uncalibrated measurement (antenna delay not yet set correctly).
- `temperature_c` is the temperature recorded at the **measuring** device at
  the time of the measurement batch (one reading per batch, not per row).
  Value is rounded to the nearest °C.
- `from_seq_id` and `to_seq_id` are assigned by the master during enumeration.
  seq_id 0 is always the main anchor itself.
- A packet with `row_count = 0` is a valid empty response (no measurements
  were obtained).
- The magic bytes allow the host to re-sync if partial data arrives.

## Distance correction

Each distance measurement has two corrections applied on the firmware side:

1. **Clock offset correction** (carrier integrator) — compensates for the
   crystal frequency difference between initiator and responder using:
   `tof = (rtd_init - rtd_resp * (1 + clock_offset_ratio)) / 2`

2. **Temperature correction** — `distance -= K * (T - T_ref)` where
   `T_ref = 23 °C`. The coefficient `K` defaults to `0.0` and can be set
   at runtime via `SET_TEMP_COEF`.

## Example

Three devices (seq_ids 0, 1, 2). Device 1 measured distances to 0 and 2
at 24 °C:

```
AA BB 02 02
01 00 E8 03 00 00 18    -- from=1, to=0, 1000 mm, T=24 C
01 02 D0 07 00 00 18    -- from=1, to=2, 2000 mm, T=24 C
```
