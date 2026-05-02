#include "configuration.h"
#include "net_mac.h"
#include "cmd_parser.h"
#include "uart.h"
#include "sleep.h"
#include "ss_twr.h"
#include "deca_device_api.h"
#include <string.h>

#define CONFIG_RETRY_MAX  3
#define CONFIG_WAIT_MS    500
#define CONFIG_POLL_MS    20

/**
 * @brief Wire-format record for a single inter-anchor distance measurement.
 *
 * Serialized as: from_seq_id (1 byte) | to_seq_id (1 byte) | distance (4 bytes, float LE).
 */
typedef struct {
	uint8_t from_seq_id; /**< seq_id of the anchor that performed the measurement */
	uint8_t to_seq_id;   /**< seq_id of the target anchor */
	float   distance;    /**< Measured distance in metres */
} measurement_t;

/* Returns the 16-bit network address stored in a device's mac_address field. */
static net_addr16_t device_addr(const net_device_t* dev)
{
	return dev->mac_address[0] | ((net_addr16_t)dev->mac_address[1] << 8);
}

static void serialize_measurement(const measurement_t* m, uint8_t* buffer, uint16_t* len)
{
	buffer[0] = m->from_seq_id;
	buffer[1] = m->to_seq_id;
	memcpy(buffer + 2, &m->distance, sizeof(float));
	*len = 2 + sizeof(float);
}

static int deserialize_measurement(const uint8_t* buffer, uint16_t len, measurement_t* m)
{
	if (len < 2 + sizeof(float))
		return -1;
	m->from_seq_id = buffer[0];
	m->to_seq_id = buffer[1];
	memcpy(&m->distance, buffer + 2, sizeof(float));
	return 0;
}

void configuration_send_measurements(net_devices_list_t* devices, net_addr16_t dst_addr)
{
	uint8_t buffer[128];
	uint16_t offset = 0;
	net_device_t* current = devices->head;

	while (current && offset < 120) {
		for (int i = 1; i <= devices->total_anchors; i++) {
			if (current->distances[i] >= 0) {
				measurement_t m;
				m.from_seq_id = current->seq_id;
				m.to_seq_id = i;
				m.distance = current->distances[i];

				uint16_t m_len;
				serialize_measurement(&m, buffer + offset, &m_len);
				offset += m_len;
			}
		}
		current = current->next;
	}

	if (offset > 0)
		net_send_to_16bit(dst_addr, buffer, offset);
}

static int handle_measurements(net_devices_list_t* devices, const uint8_t* data, uint16_t len)
{
	uint16_t offset = 0;
	int count = 0;

	while (offset + 2 + sizeof(float) <= len) {
		measurement_t m;
		if (deserialize_measurement(data + offset, len - offset, &m) != 0)
			break;
		net_device_t* from = net_device_find_by_seq(devices, m.from_seq_id);
		if (from)
			net_device_update_distance(from, m.to_seq_id, m.distance);
		offset += 2 + sizeof(float);
		count++;
	}

	return count;
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

	uart_puts("\r\n=== Starting CONFIGURATION ===\r\n");
	net_state.mode = NET_MODE_CONFIG;

	net_device_t* current = devices->head;
	while (current) {
		/* Master doesn't need external configuration — skip itself */
		if (current->device_type == DEVICE_TYPE_MAIN_ANCHOR) {
			current = current->next;
			continue;
		}

		net_addr16_t anchor_addr = device_addr(current);
		uart_printf("Configuring device seq_id=%d addr=0x%04X\r\n",
			    current->seq_id, anchor_addr);

		int got_measurements = 0;
		for (int retry = 0; retry < CONFIG_RETRY_MAX; retry++) {
			net_send_to_16bit(anchor_addr,
					  (const uint8_t*)cmd_str(CMD_CONFIG_START),
					  cmd_size(CMD_CONFIG_START));
			dwt_rxenable(DWT_START_RX_IMMEDIATE);

			for (int t = 0; t < CONFIG_WAIT_MS; t += CONFIG_POLL_MS) {
				sleep_ms(CONFIG_POLL_MS);
				net_message_t msg;
				if (net_rx_poll(&msg)) {
					if (configuration_handle_message(devices, &msg) > 0)
						got_measurements = 1;
					dwt_rxenable(DWT_START_RX_IMMEDIATE);
				}
				if (got_measurements)
					break;
			}

			if (got_measurements)
				break;

			uart_printf("Retry %d/%d for seq_id=%d\r\n",
				    retry + 1, CONFIG_RETRY_MAX, current->seq_id);
		}

		net_send_to_16bit(anchor_addr,
				  (const uint8_t*)cmd_str(CMD_CONFIG_STOP),
				  cmd_size(CMD_CONFIG_STOP));
		dwt_rxenable(DWT_START_RX_IMMEDIATE);

		current = current->next;
		sleep_ms(100);
	}
	net_state.mode = NET_MODE_IDLE;

	uart_puts("Configuration completed\r\n");
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
			if (ss_twr_measure_distance(&distance) == 0)
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
