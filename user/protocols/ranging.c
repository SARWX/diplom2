#include "ranging.h"
#include "meas_table.h"
#include "cmd_parser.h"
#include "ss_twr.h"
#include "net_mac.h"
#include "uart.h"
#include "sleep.h"
#include <string.h>

/** @brief Pause between consecutive ranging cycles (milliseconds). */
#define RANGING_CYCLE_DELAY_MS 200

/* Returns the 16-bit short address from a device's mac_address. */
static net_addr16_t dev_addr(const net_device_t* dev)
{
	return dev->mac_address[0] | ((net_addr16_t)dev->mac_address[1] << 8);
}

static void do_ranging_cycle(net_devices_list_t* devices, uint8_t own_seq_id)
{
	net_device_t* self = net_device_find_by_seq(devices, own_seq_id);
	if (!self) return;

	for (int i = 0; i < MAX_DISTANCES; i++)
		self->distances[i] = DISTANCE_INVALID;

	net_device_t* target = devices->head;
	while (target) {
		if (target->seq_id != own_seq_id &&
		    target->device_type != DEVICE_TYPE_TAG) {
			float dist;
			if (ss_twr_measure_distance(dev_addr(target), &dist) == 0)
				net_device_update_distance(self, target->seq_id, dist);
			sleep_ms(10);
		}
		target = target->next;
	}
}

static void send_results(net_devices_list_t* devices, uint8_t own_seq_id)
{
	net_device_t* self = net_device_find_by_seq(devices, own_seq_id);
	if (!self) return;

	uint8_t buf[MEAS_TABLE_HDR_SIZE + MEAS_TABLE_ROW_SIZE * MAX_DISTANCES];
	uint16_t len;
	meas_table_serialize_row(self, buf, &len);
	/* Unicast to master (always seq_id=1, short addr=0x0001) */
	net_send_to_16bit(0x0001, buf, len);
}

static int check_stop(void)
{
	net_message_t msg;
	if (!net_rx_poll(&msg)) return 0;
	if (msg.payload_len == 0) return 0;

	char cmd_buf[MAX_PAYLOAD_SIZE + 1];
	uint16_t plen = msg.payload_len < MAX_PAYLOAD_SIZE
	                ? msg.payload_len : MAX_PAYLOAD_SIZE;
	memcpy(cmd_buf, msg.payload, plen);
	cmd_buf[plen] = '\0';

	cmd_parse_result_t r = cmd_parse(cmd_buf);
	return (r.code == CMD_RANGING_STOP || r.code == CMD_STOP);
}

void ranging_run(net_devices_list_t* devices, uint8_t own_seq_id)
{
	net_state.mode = NET_MODE_RANGING;
	uart_dbg("Ranging started (seq_id=%d)\r\n", own_seq_id);

	while (net_state.mode == NET_MODE_RANGING) {
		do_ranging_cycle(devices, own_seq_id);
		send_results(devices, own_seq_id);

		/* Poll for STOP during inter-cycle delay */
		for (uint32_t t = 0; t < RANGING_CYCLE_DELAY_MS; t += 10) {
			sleep_ms(10);
			if (check_stop()) {
				net_state.mode = NET_MODE_IDLE;
				break;
			}
		}
	}

	uart_dbg("Ranging stopped\r\n");
}

void ranging_handle_message(net_devices_list_t* devices, net_message_t* msg,
                             uint8_t own_seq_id)
{
	if (!msg || msg->payload_len == 0) return;

	char cmd_buf[MAX_PAYLOAD_SIZE + 1];
	uint16_t plen = msg->payload_len < MAX_PAYLOAD_SIZE
	                ? msg->payload_len : MAX_PAYLOAD_SIZE;
	memcpy(cmd_buf, msg->payload, plen);
	cmd_buf[plen] = '\0';

	cmd_parse_result_t result = cmd_parse(cmd_buf);
	if (result.code == CMD_RANGING_START)
		ranging_run(devices, own_seq_id);
}

int ranging_handle_rx(net_devices_list_t* devices, net_message_t* msg)
{
	if (!msg || msg->payload_len < MEAS_TABLE_HDR_SIZE) return 0;
	if (msg->payload[0] != MEAS_TABLE_MAGIC_0 ||
	    msg->payload[1] != MEAS_TABLE_MAGIC_1)  return 0;

	if (meas_table_deserialize(devices, msg->payload, msg->payload_len) != 0)
		return 0;

	meas_table_print(devices);

	/* Forward binary packet to host over UART */
	for (uint16_t i = 0; i < msg->payload_len; i++)
		uart_putchar(msg->payload[i]);

	return 1;
}
