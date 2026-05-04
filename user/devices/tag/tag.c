#include "tag.h"
#include "net_mac.h"
#include "net_dispatch.h"
#include "net_devices.h"
#include "enumeration.h"
#include "configuration.h"
#include "ranging.h"
#include "cmd_parser.h"
#include <string.h>

/** @brief List of network devices received during enumeration. */
static net_devices_list_t devices;

/*==============================================================================
 * Idle-mode handler — called by net_process for NET_MODE_IDLE frames
 *============================================================================*/

static void tag_idle(net_devices_list_t* devs, net_message_t* msg)
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

	case CMD_RANGING_START:
		ranging_handle_message(devs, msg, enumeration_get_own_seq_id());
		break;

	default:
		break;
	}
}

/*==============================================================================
 * Public Functions
 *============================================================================*/

void tag_init(void)
{
	net_radio_init();
	net_devices_init(&devices);
}

void tag_loop(void)
{
	net_process(&devices, tag_idle);
}
