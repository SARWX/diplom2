#include "anchor.h"
#include "net_mac.h"
#include "ss_twr.h"
#include "net_devices.h"
#include "cmd_parser.h"
#include "enumeration.h"
#include "deca_device_api.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>

/** @brief List of network devices discovered during enumeration. */
static net_devices_list_t devices;
/** @brief Short address of the master that requested configuration; non-zero while config is pending. */
static volatile net_addr16_t pending_config_answer = NULL;
/** @brief Set to 1 in the ISR when a DISCOVER command is received; cleared in the main loop. */
static volatile uint8_t pending_enum_answer = 0;

/* IDLE state - no mode yet :( */
static void idle_handle_message(net_devices_list_t* devices, net_message_t* msg)
{
	char cmd_buffer[MAX_PAYLOAD_SIZE + 1];
	if (!msg || msg->payload_len == 0) return;

	memcpy(cmd_buffer, msg->payload, msg->payload_len);
	cmd_buffer[msg->payload_len] = '\0';
	
	cmd_parse_result_t result = cmd_parse(cmd_buffer);
	
	switch (result.code) {
		case CMD_DISCOVER:
			net_state.mode = NET_MODE_ENUMERATION;
			pending_enum_answer = 1;
			break;
		case CMD_CONFIG_START:
			net_state.mode = NET_MODE_CONFIG;
			pending_config_answer = msg->src_addr16;
			break;
		default:
			break;
	}
}

/*==============================================================================
 * RX Callback
 *============================================================================*/

static void rx_ok_cb(const dwt_cb_data_t *cb_data)
{
	net_message_t msg;
	
	if (cb_data->datalength > sizeof(net_state.rx_buffer))
		return;
	
	dwt_readrxdata(net_state.rx_buffer, cb_data->datalength, 0);
	if (!net_parse_message(net_state.rx_buffer, cb_data->datalength, &msg))
		return;
	
	/* SS TWR handling first */
	if (ss_twr_handle_rx_frame(&msg))
		goto out;

	switch (net_state.mode)
	{
	case NET_MODE_ENUMERATION:
		enumeration_handle_message(&devices, &msg);
		break;
	case NET_MODE_CONFIG:
		enumeration_handle_message(&devices, &msg);
		break;
	default:
		/* Режим пока не установлен */
		idle_handle_message(&devices, &msg);
		break;
	}

out:
	/* Re-enable reception */
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void rx_to_cb(const dwt_cb_data_t *cb_data)
{
	(void)cb_data;
	if (net_state.mode == NET_MODE_IDLE)
		dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void rx_err_cb(const dwt_cb_data_t *cb_data)
{
	(void)cb_data;
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

/*==============================================================================
 * Public Functions
 *============================================================================*/

void anchor_init(void)
{
	port_set_deca_isr(dwt_isr);
	dwt_setcallbacks(NULL, rx_ok_cb, rx_to_cb, rx_err_cb);
	dwt_setinterrupt(DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE |
						DWT_INT_RFSL | DWT_INT_RFTO , 1);
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
	
	net_devices_init(&devices);
}

void anchor_loop(void)
{
	if (pending_enum_answer) {
		handle_discover(&devices);
		pending_enum_answer = 0;
	}

	if (pending_config_answer) {
		configuration_perform_measurements(&devices, pending_config_answer);
		configuration_send_measurements(&devices, pending_config_answer);
		pending_config_answer = NULL;
	}
}
