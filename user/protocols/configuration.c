#include "configuration.h"
#include "meas_table.h"
#include "enumeration.h"
#include "net_mac.h"
#include "cmd_parser.h"
#include "uart.h"
#include "sleep.h"
#include "ss_twr.h"
#include "deca_device_api.h"
#include <string.h>

#define CONFIG_RETRY_MAX  3
#define CONFIG_WAIT_MS    2000
#define CONFIG_POLL_MS    20

/* Returns the 16-bit network address stored in a device's mac_address field. */
static net_addr16_t device_addr(const net_device_t* dev)
{
	return dev->mac_address[0] | ((net_addr16_t)dev->mac_address[1] << 8);
}

void configuration_send_measurements(net_devices_list_t* devices, net_addr16_t dst_addr)
{
	uint8_t buf[MEAS_TABLE_HDR_SIZE + MEAS_TABLE_ROW_SIZE * MAX_DISTANCES];
	uint16_t len;
	net_device_t* self = net_device_find_by_seq(devices, enumeration_get_own_seq_id());
	if (!self) return;
	meas_table_serialize_row(self, buf, &len);
	net_send_to_16bit(dst_addr, buf, len);
}

static int handle_measurements(net_devices_list_t* devices, const uint8_t* data, uint16_t len)
{
	if (meas_table_deserialize(devices, data, len) != 0)
		return 0;
	return 1;
}

/*==============================================================================
 * Master
 *============================================================================*/

int configuration_start_master(net_devices_list_t* devices)
{
	if (!devices->initialized || devices->total_anchors == 0) {
		uart_puts("ERROR: System not initialized or no devices\r\n");
		return -1;
	}

	uart_dbg("\r\n=== Starting CONFIGURATION ===\r\n");
	net_state.mode = NET_MODE_CONFIG;

	net_device_t* current = devices->head;
	while (current) {
		/* Master doesn't need external configuration — skip itself */
		if (current->device_type == DEVICE_TYPE_MAIN_ANCHOR) {
			current = current->next;
			continue;
		}

		net_addr16_t anchor_addr = device_addr(current);
		uart_dbg("Configuring device seq_id=%d addr=0x%04X\r\n",
			 current->seq_id, anchor_addr);

		int got_measurements = 0;
		for (int retry = 0; retry < CONFIG_RETRY_MAX; retry++) {
			net_send_to_16bit(anchor_addr,
					  (const uint8_t*)cmd_str(CMD_CONFIG_START),
					  cmd_size(CMD_CONFIG_START));

			for (int t = 0; t < CONFIG_WAIT_MS; t += CONFIG_POLL_MS) {
				sleep_ms(CONFIG_POLL_MS);
				net_message_t msg;
				if (net_rx_poll(&msg)) {
					int r = configuration_handle_message(devices, &msg);
					uart_dbg("Config rx: src=0x%04X r=%d len=%d\r\n",
					         (unsigned)msg.src_addr16, r, msg.payload_len);
					if (r > 0)
						got_measurements = 1;
				}
				if (got_measurements)
					break;
			}

			if (got_measurements)
				break;

			uart_dbg("Retry %d/%d for seq_id=%d\r\n",
				 retry + 1, CONFIG_RETRY_MAX, current->seq_id);
		}

		net_send_to_16bit(anchor_addr,
				  (const uint8_t*)cmd_str(CMD_CONFIG_STOP),
				  cmd_size(CMD_CONFIG_STOP));

		current = current->next;
		sleep_ms(100);
	}
	net_state.mode = NET_MODE_IDLE;

	uart_dbg("Configuration completed\r\n");
	return 0;
}

/*==============================================================================
 * Anchor
 *============================================================================*/

void configuration_perform_measurements(net_devices_list_t* devices, uint8_t my_seq_id)
{
	net_device_t* my_device = net_device_find_by_seq(devices, my_seq_id);
	if (!my_device)
		return;

	net_device_t* target = devices->head;
	while (target) {
		if (target->seq_id != my_seq_id) {
			float distance;
			if (ss_twr_measure_distance(device_addr(target), &distance) == 0)
				net_device_update_distance(my_device, target->seq_id, distance);
			sleep_ms(50);
		}
		target = target->next;
	}
}

/*==============================================================================
 * Common message handler — call from main loop context only
 *============================================================================*/

int configuration_handle_message(net_devices_list_t* devices, net_message_t* msg)
{
	if (!msg || msg->payload_len == 0)
		return 0;

	char cmd_buffer[MAX_PAYLOAD_SIZE + 1];
	uint16_t plen = msg->payload_len < MAX_PAYLOAD_SIZE ? msg->payload_len : MAX_PAYLOAD_SIZE;
	memcpy(cmd_buffer, msg->payload, plen);
	cmd_buffer[plen] = '\0';

	cmd_parse_result_t result = cmd_parse(cmd_buffer);

	switch (result.code) {
	case CMD_CONFIG_STOP:
		net_state.mode = NET_MODE_IDLE;
		return 0;
	default:
		/* Treat as a measurement packet */
		return handle_measurements(devices, msg->payload, msg->payload_len) > 0 ? 1 : 0;
	}
}
