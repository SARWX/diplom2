#ifndef MEAS_TABLE_H
#define MEAS_TABLE_H

/**
 * @file meas_table.h
 * @brief Serialization of inter-device distance measurements.
 *
 * Binary format (all fields little-endian):
 *
 * @code
 * Offset     Size  Field
 * ---------  ----  ---------------------------------------------------
 * 0          1     magic[0] = 0xAA
 * 1          1     magic[1] = 0xBB
 * 2          1     version  = 1
 * 3          1     row_count
 * 4 + 6*i   1     from_seq_id
 * 5 + 6*i   1     to_seq_id
 * 6 + 6*i   4     distance_mm (int32, signed, little-endian)
 * @endcode
 *
 * Total size: 4 + row_count * 6 bytes.
 * Negative distance_mm values are valid (uncalibrated measurements).
 */

#include "net_devices.h"
#include <stdint.h>

#define MEAS_TABLE_MAGIC_0  0xAAu
#define MEAS_TABLE_MAGIC_1  0xBBu
#define MEAS_TABLE_VERSION  1u
#define MEAS_TABLE_HDR_SIZE 4u
#define MEAS_TABLE_ROW_SIZE 6u

/**
 * @brief Serialize the distance table of a single device into @p buf.
 *
 * Writes a complete measurement-table packet for all valid distances
 * stored in @p dev. Entries where distance equals DISTANCE_INVALID are
 * skipped.
 *
 * @param dev  Source device whose distances[] array is serialized.
 * @param buf  Output buffer. Must be at least
 *             MEAS_TABLE_HDR_SIZE + MEAS_TABLE_ROW_SIZE * MAX_DISTANCES bytes.
 * @param len  Set to the number of bytes written.
 */
void meas_table_serialize_row(const net_device_t* dev,
                               uint8_t* buf, uint16_t* len);

/**
 * @brief Serialize the full distance table for all devices in @p list.
 *
 * Writes a single packet containing every valid measurement across all
 * devices in the list.
 *
 * @param list Source device list.
 * @param buf  Output buffer. Must be at least
 *             MEAS_TABLE_HDR_SIZE +
 *             MEAS_TABLE_ROW_SIZE * MAX_DISTANCES * list->total_anchors bytes.
 * @param len  Set to the number of bytes written.
 */
void meas_table_serialize(const net_devices_list_t* list,
                           uint8_t* buf, uint16_t* len);

/**
 * @brief Deserialize a measurement-table packet into @p list.
 *
 * Finds each referenced device by seq_id and updates its distances[].
 * Unknown seq_ids are silently ignored.
 *
 * @param list  Device list to update.
 * @param buf   Input buffer.
 * @param len   Buffer length in bytes.
 * @return 0 on success, -1 if the packet is malformed or magic/version mismatch.
 */
int meas_table_deserialize(net_devices_list_t* list,
                            const uint8_t* buf, uint16_t len);

/**
 * @brief Print the full distance table over UART (debug output).
 *
 * Calls uart_dbg() — no-op if debug is disabled.
 *
 * @param list Device list to print.
 */
void meas_table_print(const net_devices_list_t* list);

#endif /* MEAS_TABLE_H */
