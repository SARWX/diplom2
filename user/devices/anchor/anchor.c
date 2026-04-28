#include "anchor.h"
#include "net_mac.h"
#include "ss_twr.h"
#include "net_devices.h"
#include "cmd_parser.h"
#include "enumeration.h"
#include "configuration.h"
#include "deca_device_api.h"
#include "port.h"
#include <string.h>

/** @brief List of network devices discovered during enumeration. */
static net_devices_list_t devices;

/*==============================================================================
 * ISR Callbacks — top half only
 *============================================================================*/

static void rx_ok_cb(const dwt_cb_data_t *cb_data)  { net_rx_ok_isr(cb_data); }
static void rx_to_cb(const dwt_cb_data_t *cb_data)  { (void)cb_data; dwt_rxenable(DWT_START_RX_IMMEDIATE); }
static void rx_err_cb(const dwt_cb_data_t *cb_data) { (void)cb_data; dwt_rxenable(DWT_START_RX_IMMEDIATE); }

/*==============================================================================
 * Public Functions
 *============================================================================*/

void anchor_init(void)
{
	port_set_deca_isr(dwt_isr);
	dwt_setcallbacks(NULL, rx_ok_cb, rx_to_cb, rx_err_cb);
	dwt_setinterrupt(DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE |
	                 DWT_INT_RFSL | DWT_INT_RFTO, 1);

	net_devices_init(&devices);
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

void anchor_loop(void)
{
	net_message_t msg;
	if (!net_rx_poll(&msg))
		return;

	/* SS-TWR has priority — handles poll/response frames inline */
	if (ss_twr_handle_rx_frame(&msg))
		goto rearm;

	switch (net_state.mode) {
	case NET_MODE_ENUMERATION:
	case NET_MODE_SYNC_WAIT:
		enumeration_handle_message(&devices, &msg);
		break;

	case NET_MODE_CONFIG:
		configuration_handle_message(&devices, &msg);
		break;

	case NET_MODE_IDLE:
	default: {
		char cmd_buf[MAX_PAYLOAD_SIZE + 1];
		uint16_t plen = msg.payload_len < MAX_PAYLOAD_SIZE ? msg.payload_len : MAX_PAYLOAD_SIZE;
		memcpy(cmd_buf, msg.payload, plen);
		cmd_buf[plen] = '\0';

		cmd_parse_result_t result = cmd_parse(cmd_buf);
		switch (result.code) {
		case CMD_DISCOVER:
			net_state.mode = NET_MODE_ENUMERATION;
			enumeration_handle_message(&devices, &msg);
			break;

		case CMD_CONFIG_START: {
			net_addr16_t master_addr = msg.src_addr16;
			net_state.mode = NET_MODE_CONFIG;
			/* Runs in main context — blocking is acceptable during config */
			configuration_perform_measurements(&devices, enumeration_get_own_seq_id());
			configuration_send_measurements(&devices, master_addr);
			net_state.mode = NET_MODE_IDLE;
			break;
		}
		default:
			break;
		}
		break;
	}
	}

rearm:
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}
