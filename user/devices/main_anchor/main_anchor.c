#include "main_anchor.h"
#include "net_devices.h"
#include "net_mac.h"
#include "ss_twr.h"
#include "uart.h"
#include "cmd_parser.h"
#include "enumeration.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>

/** @brief List of network devices discovered during enumeration. */
static net_devices_list_t devices;
/** @brief Non-zero when verbose debug output is enabled via CMD_DEBUG_ON. */
static uint8_t debug_enabled = 0;

/*==============================================================================
 * Command Handlers
 *============================================================================*/

static void handle_initialize(void)
{
	uart_puts("\r\n>>> Handling INITIALIZE command\r\n");
	
	if (enumeration_start_master(&devices) == 0) {
		uart_puts("System initialized successfully\r\n");
	} else {
		uart_puts("ERROR: System initialization failed\r\n");
	}
}

static void handle_reset(void)
{
	uart_puts("\r\n>>> Handling RESET command\r\n");
	net_devices_clear(&devices);
	devices.initialized = 0;
	uart_puts("System reset complete\r\n");
}

static void handle_get_status(void)
{
	uart_puts("\r\n>>> Handling GET STATUS command\r\n");
	uart_printf("Initialized: %s\r\n", devices.initialized ? "YES" : "NO");
	uart_printf("Total devices: %d\r\n", devices.total_anchors);
	uart_printf("Debug mode: %s\r\n", debug_enabled ? "ON" : "OFF");
	net_devices_print(&devices);
}

static void handle_debug(uint8_t enable)
{
	debug_enabled = enable;
	net_devices_set_debug(enable);
	uart_printf("Debug mode %s\r\n", enable ? "enabled" : "disabled");
}

static void process_command(cmd_parse_result_t cmd)
{
	if (!cmd.valid) {
		uart_puts("ERROR: Invalid command\r\n");
		return;
	}
	
	switch (cmd.code) {
		case CMD_INITIALIZE:    handle_initialize(); break;
		case CMD_RESET:         handle_reset(); break;
		case CMD_GET_STATUS:    handle_get_status(); break;
		case CMD_DEBUG_ON:      handle_debug(1); break;
		case CMD_DEBUG_OFF:     handle_debug(0); break;
		default:
			uart_printf("Command not implemented: %s\r\n", cmd_str(cmd.code));
			break;
	}
}

/*==============================================================================
 * DW1000 Callbacks
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
		return 0;

	switch (net_state.mode)
	{
	case NET_MODE_ENUMERATION:
	case NET_MODE_SYNC_WAIT:
		enumeration_handle_message(&devices, &msg);
		break;
	
	default:
		break;
	}
	
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void rx_err_cb(const dwt_cb_data_t *cb_data)
{
	(void)cb_data;
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

/*==============================================================================
 * Device Interface
 *============================================================================*/

void main_anchor_init(void)
{
	uart_init(115200);
	
	port_set_deca_isr(dwt_isr);
	dwt_setcallbacks(NULL, rx_ok_cb, NULL, rx_err_cb);
	dwt_setinterrupt(DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFSL, 1);
	dwt_rxenable(DWT_START_RX_IMMEDIATE);

	net_devices_init(&devices);
	
	uart_puts("\r\n========================================\r\n");
	uart_puts("Main Anchor Station\r\n");
	uart_puts("========================================\r\n");
	uart_puts("Commands: INITIALIZE, RESET, GET_STATUS, DEBUG_ON/OFF\r\n");
	uart_puts("> ");
}

void main_anchor_loop(void)
{
	static char line_buffer[128];
	
	uart_readline(line_buffer, sizeof(line_buffer));
	cmd_parse_result_t cmd = cmd_parse(line_buffer);
	process_command(cmd);
	
	uart_puts("\r\n> ");
}
