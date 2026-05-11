#include "anchor.h"
#include "net_mac.h"
#include "net_dispatch.h"
#include "net_devices.h"
#include "enumeration.h"
#include "configuration.h"
#include "cmd_parser.h"
#include "ss_twr.h"
#include <string.h>

static int parse_uint_a(const char** p)
{
	while (**p == ' ' || **p == '\t') (*p)++;
	if (**p < '0' || **p > '9') return -1;
	int val = 0;
	while (**p >= '0' && **p <= '9') { val = val * 10 + (**p - '0'); (*p)++; }
	return val;
}

/** @brief List of network devices discovered during enumeration. */
static net_devices_list_t devices;

/*==============================================================================
 * Idle-mode handler — called by net_process for NET_MODE_IDLE frames
 *============================================================================*/

static void anchor_idle(net_devices_list_t *devs, net_message_t *msg)
{
	char cmd_buf[MAX_PAYLOAD_SIZE + 1];
	uint16_t plen = msg->payload_len < MAX_PAYLOAD_SIZE
			? msg->payload_len : MAX_PAYLOAD_SIZE;
	memcpy(cmd_buf, msg->payload, plen);
	cmd_buf[plen] = '\0';

	cmd_parse_result_t result = cmd_parse(cmd_buf);
	switch (result.code) {
	case CMD_DISCOVER:
		net_state.mode = NET_MODE_ENUMERATION;
		enumeration_handle_message(devs, msg);
		break;

	case CMD_CONFIG_START: {
		net_addr16_t master_addr = msg->src_addr16;
		net_state.mode = NET_MODE_CONFIG;
		configuration_perform_measurements(devs, enumeration_get_own_seq_id());
		configuration_send_measurements(devs, master_addr);
		net_state.mode = NET_MODE_IDLE;
		break;
	}
	case CMD_SET_ANT_DLY: {
		const char* p = result.args;
		int tx_dly = parse_uint_a(&p);
		int rx_dly = parse_uint_a(&p);
		if (tx_dly >= 0 && rx_dly >= 0)
			ss_twr_set_ant_dly((uint16_t)tx_dly, (uint16_t)rx_dly);
		break;
	}
	case CMD_SET_TEMP_COEF: {
		const char* p = result.args;
		int k_per_mil = parse_uint_a(&p);
		if (k_per_mil >= 0)
			ss_twr_set_temp_coef((float)k_per_mil * 1e-6f);
		break;
	}
	default:
		break;
	}
}

/*==============================================================================
 * Public Functions
 *============================================================================*/

void anchor_init(void)
{
	net_radio_init();
	net_devices_init(&devices);
}

void anchor_loop(void)
{
	net_process(&devices, anchor_idle);
}
