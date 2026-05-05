# UART Command Protocol

The main anchor communicates with the host over UART at **115200 8N1**.

All commands and responses are ASCII text terminated with `\r\n`, except where
binary payloads are specified.

---

## Handshake

To detect the device before issuing commands, the host sends the identification
string and the device replies with a short token.

| Direction    | Data           |
|--------------|----------------|
| Host → Device | `LOC_POS_SYS\r\n` |
| Device → Host | `LPS\r\n`       |

---

## Response Codes

Every command receives exactly one of these terminal responses after it
completes (or fails):

| Response     | Meaning                          |
|--------------|----------------------------------|
| `OK\r\n`     | Command completed successfully   |
| `ERR\r\n`    | Command failed                   |
| `UNK\r\n`    | Command not recognized           |

Binary payloads (described per-command) are sent **before** the terminal
response line.

Debug lines (enabled with `DEBUG_ON`) are human-readable text prefixed with
arbitrary content. They are interleaved between the binary payload and the
terminal response and must be ignored by the host parser.

---

## Commands

### `INITIALIZE`

Runs full system initialisation: node discovery (enumeration) followed by
inter-anchor distance measurement (configuration).

**Response on success:**

```
<binary meas_table>
OK\r\n
```

**Response on failure:**

```
ERR\r\n
```

See [meas_table_format.md](meas_table_format.md) for the binary payload layout.

---

### `RECONFIGURE`

Re-runs the configuration phase (distance measurement) without re-running
enumeration. Requires a prior successful `INITIALIZE`.

**Response on success:**

```
<binary meas_table>
OK\r\n
```

**Response on failure (not initialized, or measurement error):**

```
ERR\r\n
```

---

### `RESET`

Clears all discovered devices and resets the initialized flag. The system must
be re-initialized before ranging can start.

**Response:** `OK\r\n`

---

### `GET_STATUS`

Returns the current system state.

**Response:**

```
OK <initialized> <device_count>\r\n
```

| Field           | Type | Description                                         |
|-----------------|------|-----------------------------------------------------|
| `initialized`   | `0`/`1` | Whether `INITIALIZE` has completed successfully  |
| `device_count`  | decimal integer | Total number of discovered anchors       |

Example: `OK 1 3\r\n` — initialized, 3 anchors found.

---

### `RANGING_START`

Instructs all anchors and the tag to begin continuous distance measurement.
The tag measures distances to all known anchors and streams the results to the
main anchor, which forwards each packet over UART as a binary measurement table
row (same format as the `INITIALIZE` payload but for a single device).

**No immediate response is sent.** Binary packets arrive asynchronously until
the host sends `STOP`.

---

### `STOP` / `RANGING_STOP`

Stops continuous ranging on all nodes.

**Response:** `OK\r\n`

---

### `DEBUG_ON` / `DEBUG_OFF`

Enable or disable human-readable debug output on the UART line. Debug is
enabled by default after power-on.

**Response:** `OK DEBUG_ON\r\n` or `OK DEBUG_OFF\r\n`

---

### `TEST_SS_TWR`

Performs a single SS-TWR ranging exchange with device address `2` and prints
the result as a debug line. Intended for development/calibration only.

**Response on success:** `OK\r\n` (distance logged via debug output)  
**Response on failure:** `ERR\r\n`

---

## Ranging Packet Stream

While ranging is active the main anchor emits binary packets in the measurement
table row format (see [meas_table_format.md](meas_table_format.md)).

Each packet contains the distances measured by the tag to all anchors in a
single ranging cycle. The host accumulates these packets until `STOP` is sent,
at which point the device replies `OK\r\n`.

### Host-side flow

```
Host                      Device
 |                            |
 |--- INITIALIZE\r\n -------->|
 |<-- <binary meas_table> ----|
 |<-- OK\r\n -----------------|
 |                            |
 |--- RANGING_START\r\n ----->|
 |<-- <binary row packet> ----|  (repeats every ~200 ms)
 |<-- <binary row packet> ----|
 |         ...                |
 |--- STOP\r\n -------------->|
 |<-- OK\r\n -----------------|
```
