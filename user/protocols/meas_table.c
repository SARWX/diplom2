#include "meas_table.h"
#include "uart.h"
#include <string.h>

static void write_row(uint8_t from_id, uint8_t to_id, float distance_m,
                      uint8_t* buf, uint16_t* offset)
{
	int32_t mm = (int32_t)(distance_m * 1000.0f);
	buf[(*offset)++] = from_id;
	buf[(*offset)++] = to_id;
	buf[(*offset)++] = (uint8_t)(mm);
	buf[(*offset)++] = (uint8_t)(mm >> 8);
	buf[(*offset)++] = (uint8_t)(mm >> 16);
	buf[(*offset)++] = (uint8_t)(mm >> 24);
}

static uint16_t write_header(uint8_t* buf)
{
	buf[0] = MEAS_TABLE_MAGIC_0;
	buf[1] = MEAS_TABLE_MAGIC_1;
	buf[2] = MEAS_TABLE_VERSION;
	buf[3] = 0; /* row_count filled in later */
	return MEAS_TABLE_HDR_SIZE;
}

void meas_table_serialize_row(const net_device_t* dev,
                               uint8_t* buf, uint16_t* len)
{
	if (!dev || !buf || !len) return;

	uint16_t offset = write_header(buf);
	uint8_t rows = 0;

	for (int i = 0; i < MAX_DISTANCES; i++) {
		if (i == dev->seq_id) continue;
		if (dev->distances[i] == DISTANCE_INVALID) continue;
		write_row(dev->seq_id, (uint8_t)i, dev->distances[i], buf, &offset);
		rows++;
	}

	buf[3] = rows;
	*len = offset;
}

void meas_table_serialize(const net_devices_list_t* list,
                           uint8_t* buf, uint16_t* len)
{
	if (!list || !buf || !len) return;

	uint16_t offset = write_header(buf);
	uint8_t rows = 0;

	const net_device_t* dev = list->head;
	while (dev) {
		for (int i = 0; i < MAX_DISTANCES; i++) {
			if (i == dev->seq_id) continue;
			if (dev->distances[i] == DISTANCE_INVALID) continue;
			write_row(dev->seq_id, (uint8_t)i, dev->distances[i], buf, &offset);
			rows++;
		}
		dev = dev->next;
	}

	buf[3] = rows;
	*len = offset;
}

int meas_table_deserialize(net_devices_list_t* list,
                            const uint8_t* buf, uint16_t len)
{
	if (!list || !buf) return -1;
	if (len < MEAS_TABLE_HDR_SIZE) return -1;
	if (buf[0] != MEAS_TABLE_MAGIC_0 || buf[1] != MEAS_TABLE_MAGIC_1) return -1;
	if (buf[2] != MEAS_TABLE_VERSION) return -1;

	uint8_t row_count = buf[3];
	if (len < MEAS_TABLE_HDR_SIZE + (uint16_t)row_count * MEAS_TABLE_ROW_SIZE)
		return -1;

	uint16_t offset = MEAS_TABLE_HDR_SIZE;
	for (uint8_t i = 0; i < row_count; i++) {
		uint8_t from_id = buf[offset++];
		uint8_t to_id   = buf[offset++];
		int32_t mm = (int32_t)(
			(uint32_t)buf[offset]       |
			(uint32_t)buf[offset + 1] << 8  |
			(uint32_t)buf[offset + 2] << 16 |
			(uint32_t)buf[offset + 3] << 24);
		offset += 4;

		net_device_t* dev = net_device_find_by_seq(list, from_id);
		if (dev && to_id < MAX_DISTANCES)
			dev->distances[to_id] = (float)mm / 1000.0f;
	}

	return 0;
}

void meas_table_print(const net_devices_list_t* list)
{
	if (!list) return;
	uart_dbg("\r\n=== Distance Table ===\r\n");
	const net_device_t* dev = list->head;
	while (dev) {
		for (int j = 0; j < MAX_DISTANCES; j++) {
			if (j == dev->seq_id) continue;
			if (dev->distances[j] == DISTANCE_INVALID) continue;
			uart_dbg("  %d -> %d : %.3f m\r\n",
			         dev->seq_id, j, dev->distances[j]);
		}
		dev = dev->next;
	}
	uart_dbg("======================\r\n");
}
