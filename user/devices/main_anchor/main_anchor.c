#include "main_anchor.h"
#include "net_devices.h"
#include "net_mac.h"
#include "net_dispatch.h"
#include "enumeration.h"
#include "configuration.h"
#include "meas_table.h"
#include "ranging.h"
#include "ss_twr.h"
#include "uart.h"
#include "cmd_parser.h"
#include "deca_device_api.h"
#include <string.h>

/** @brief List of network devices discovered during enumeration. */
static net_devices_list_t devices;

/*==============================================================================
 * Protocol responses
 *============================================================================*/

#define REPLY_OK   "OK\r\n"
#define REPLY_ERR  "ERR\r\n"
#define REPLY_UNK  "UNK\r\n"

static void reply_ok(void)  { uart_puts(REPLY_OK); }
static void reply_err(void) { uart_puts(REPLY_ERR); }
static void reply_unk(void) { uart_puts(REPLY_UNK); }

static void send_table_uart(void)
{
	uint8_t buf[MEAS_TABLE_HDR_SIZE + MEAS_TABLE_ROW_SIZE * MAX_DISTANCES * MAX_ANCHORS];
	uint16_t len;
	meas_table_serialize(&devices, buf, &len);
	for (uint16_t i = 0; i < len; i++)
		uart_putchar(buf[i]);
}

/*==============================================================================
 * Command Handlers
 *============================================================================*/

static void handle_ping(void)
{
	uart_puts("LPS\r\n");
}

static void handle_initialize(void)
{
	uart_dbg(">>> INITIALIZE\r\n");

	if (enumeration_start_master(&devices) != 0) {
		uart_dbg("Enumeration failed\r\n");
		reply_err();
		return;
	}

	if (configuration_start_master(&devices) != 0) {
		uart_dbg("Configuration failed\r\n");
		reply_err();
		return;
	}

	uart_dbg("Master measuring distances...\r\n");
	configuration_perform_measurements(&devices, enumeration_get_own_seq_id());

	meas_table_print(&devices);
	send_table_uart();
	reply_ok();
}

static void handle_reconfigure(void)
{
	uart_dbg(">>> RECONFIGURE\r\n");

	if (!devices.initialized) {
		uart_dbg("Not initialized\r\n");
		reply_err();
		return;
	}

	if (configuration_start_master(&devices) != 0) {
		uart_dbg("Configuration failed\r\n");
		reply_err();
		return;
	}

	uart_dbg("Master measuring distances...\r\n");
	configuration_perform_measurements(&devices, enumeration_get_own_seq_id());

	meas_table_print(&devices);
	send_table_uart();
	reply_ok();
}

static void handle_reset(void)
{
	uart_dbg(">>> RESET\r\n");
	net_devices_clear(&devices);
	devices.initialized = 0;
	reply_ok();
}

static void handle_get_status(void)
{
	uart_dbg(">>> GET_STATUS\r\n");
	uart_dbg("Initialized: %s\r\n", devices.initialized ? "YES" : "NO");
	uart_dbg("Total devices: %d\r\n", devices.total_anchors);
	net_devices_print(&devices);
	/* Reply: "OK <initialized> <device_count>\r\n" */
	uart_printf("OK %d %d\r\n", devices.initialized, devices.total_anchors);
}

static void handle_test_ss_twr(void)
{
	uart_dbg(">>> TEST_SS_TWR\r\n");
	float dist;
	if (ss_twr_measure_distance(2, &dist) == 0) {
		uart_dbg("dist=%.3f m\r\n", dist);
		reply_ok();
	} else {
		reply_err();
	}
}

static void handle_ranging_start(void)
{
	uart_dbg(">>> RANGING_START\r\n");
	if (!devices.initialized) {
		uart_dbg("Not initialized\r\n");
		reply_err();
		return;
	}
	net_send_broadcast((const uint8_t*)cmd_str(CMD_RANGING_START),
	                   cmd_size(CMD_RANGING_START));
	/* OK is sent after ranging stops (see CMD_RANGING_STOP / CMD_STOP) */
}

static void handle_ranging_stop(void)
{
	uart_dbg(">>> RANGING_STOP\r\n");
	net_send_broadcast((const uint8_t*)cmd_str(CMD_RANGING_STOP),
	                   cmd_size(CMD_RANGING_STOP));
	reply_ok();
}

static void handle_debug(uint8_t enable)
{
	uart_dbg_set(enable);
	uart_printf("OK DEBUG_%s\r\n", enable ? "ON" : "OFF");
}

static void process_command(cmd_parse_result_t cmd)
{
	if (!cmd.valid) {
		reply_unk();
		return;
	}

	switch (cmd.code) {
	case CMD_PING:           handle_ping();           break;
	case CMD_INITIALIZE:     handle_initialize();     break;
	case CMD_RECONFIGURE:    handle_reconfigure();    break;
	case CMD_RESET:          handle_reset();          break;
	case CMD_GET_STATUS:     handle_get_status();     break;
	case CMD_DEBUG_ON:       handle_debug(1);         break;
	case CMD_DEBUG_OFF:      handle_debug(0);         break;
	case CMD_TEST_SS_TWR:    handle_test_ss_twr();    break;
	case CMD_RANGING_START:  handle_ranging_start();  break;
	case CMD_RANGING_STOP:
	case CMD_STOP:           handle_ranging_stop();   break;
	default:                 reply_unk();             break;
	}
}

/*==============================================================================
 * Device Interface
 *============================================================================*/

void main_anchor_init(void)
{
	uart_init(115200);
	net_radio_init();
	net_devices_init(&devices);

	uart_dbg("\r\n========================================\r\n");
	uart_dbg("Main Anchor Station\r\n");
	uart_dbg("Commands: LOC_POS_SYS INITIALIZE RECONFIGURE RESET GET_STATUS\r\n");
	uart_dbg("          DEBUG_ON DEBUG_OFF TEST_SS_TWR RANGING_START STOP\r\n");
	uart_dbg("========================================\r\n");
}

static void main_anchor_idle(net_devices_list_t* devs, net_message_t* msg)
{
	ranging_handle_rx(devs, msg);
}

static void poll_network(void)
{
	net_process(&devices, main_anchor_idle);
}

void main_anchor_loop(void)
{
	static char line_buf[128];
	uart_readline_idle(line_buf, sizeof(line_buf), poll_network);
	process_command(cmd_parse(line_buf));
}
