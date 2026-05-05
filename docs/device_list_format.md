# Device List Binary Format

The device list packet describes all nodes discovered during enumeration:
their sequential IDs, types, and MAC addresses.

Magic bytes distinguish this packet from the measurement table packet on the
host side.

## Layout

All multi-byte fields are little-endian unless noted otherwise.

```
Offset       Size  Field
-----------  ----  -----------------------------------------------
0            1     magic[0] = 0xAA
1            1     magic[1] = 0xCC
2            1     version  = 1
3            1     device_count  (N)
4 + 8*i      1     seq_id        — sequential ID (1-based, assigned during enumeration)
5 + 8*i      1     device_type   — see table below
6 + 8*i      6     mac_address   — 6-byte MAC (same byte order as on-air EUI-64 low 6 bytes)
```

Total size: `4 + N * 8` bytes.

## `device_type` Values

| Value | Constant          | Meaning                                      |
|-------|-------------------|----------------------------------------------|
| `0`   | `NONE`            | Unknown / unregistered device                |
| `1`   | `MAIN_ANCHOR`     | Master anchor station (has UART host port)   |
| `2`   | `ANCHOR`          | Regular anchor (participates in ranging)     |
| `3`   | `TAG`             | Mobile tag being located                     |

## Example

A system with 1 main anchor (seq 1), 2 anchors (seq 2, 3), and 1 tag (seq 4):

```
AA CC 01 04                   -- magic, version=1, device_count=4
01 01 AA BB CC DD EE FF       -- seq=1, MAIN_ANCHOR, MAC=AA:BB:CC:DD:EE:FF
02 02 11 22 33 44 55 66       -- seq=2, ANCHOR
03 02 AA BB 11 22 33 44       -- seq=3, ANCHOR
04 03 DE AD BE EF 00 01       -- seq=4, TAG
```
