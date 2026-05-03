#include "anchor.h"
#include "net_dispatch.h"
#include "net_devices.h"
#include "net_mac.h"
#include "enumeration.h"
#include "cmd_parser.h"
#include "deca_device_api.h"
#include <string.h>

#define TX_ANT_DLY 16724
#define RX_ANT_DLY 16724

static net_devices_list_t devices;

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
	default:
		break;
	}
}

void anchor_init(void)
{
	net_radio_init();
	dwt_setrxantennadelay(RX_ANT_DLY);
	dwt_settxantennadelay(TX_ANT_DLY);
	net_state.short_addr = 2;
	dwt_setaddress16(2);
	net_devices_init(&devices);
}

void anchor_loop(void)
{
	net_process(&devices, anchor_idle);
}
