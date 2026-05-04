#include "main_anchor.h"
#include "net_devices.h"
#include "net_mac.h"
#include "net_dispatch.h"
#include "enumeration.h"
#include "configuration.h"
#include "ss_twr.h"
#include "uart.h"
#include "cmd_parser.h"
#include "deca_device_api.h"
#include <string.h>

/** @brief List of network devices discovered during enumeration. */
static net_devices_list_t devices;
/** @brief Non-zero when verbose debug output is enabled via CMD_DEBUG_ON. */
static uint8_t debug_enabled = 0;

/*==============================================================================
 * Command Handlers
 *============================================================================*/

static void print_distance_table(void)
{
	uart_puts("\r\n=== Distance Table ===\r\n");
	net_device_t* dev = devices.head;
	while (dev) {
		for (int j = 1; j <= devices.total_anchors; j++) {
			if (j == dev->seq_id) continue;
			if (dev->distances[j] == DISTANCE_INVALID) continue;
			uart_printf("  %d -> %d : %.3f m\r\n",
			            dev->seq_id, j, dev->distances[j]);
		}
		dev = dev->next;
	}
	uart_puts("======================\r\n");
}

static void run_own_measurements(void)
{
	uart_puts("Master measuring distances...\r\n");
	configuration_perform_measurements(&devices, enumeration_get_own_seq_id());
}

static void handle_initialize(void)
{
	uart_puts("\r\n>>> INITIALIZE\r\n");

	if (enumeration_start_master(&devices) != 0) {
		uart_puts("ERROR: Enumeration failed\r\n");
		return;
	}

	if (configuration_start_master(&devices) != 0) {
		uart_puts("ERROR: Configuration failed\r\n");
		return;
	}

	run_own_measurements();
	uart_puts("System initialized successfully\r\n");
	print_distance_table();
}

static void handle_reconfigure(void)
{
	uart_puts("\r\n>>> RECONFIGURE\r\n");

	if (!devices.initialized) {
		uart_puts("ERROR: Run INITIALIZE first\r\n");
		return;
	}

	if (configuration_start_master(&devices) != 0) {
		uart_puts("ERROR: Reconfiguration failed\r\n");
		return;
	}

	run_own_measurements();
	uart_puts("Reconfiguration complete\r\n");
	print_distance_table();
}

static void handle_reset(void)
{
	uart_puts("\r\n>>> RESET\r\n");
	net_devices_clear(&devices);
	devices.initialized = 0;
	uart_puts("System reset complete\r\n");
}

static void handle_get_status(void)
{
	uart_puts("\r\n>>> GET_STATUS\r\n");
	uart_printf("Initialized: %s\r\n", devices.initialized ? "YES" : "NO");
	uart_printf("Total devices: %d\r\n", devices.total_anchors);
	uart_printf("Debug mode: %s\r\n", debug_enabled ? "ON" : "OFF");
	net_devices_print(&devices);
}

static void handle_test_ss_twr(void)
{
	uart_puts("\r\n>>> TEST_SS_TWR\r\n");
	float dist;
	if (ss_twr_measure_distance(2, &dist) == 0)
		uart_printf("dist=%.2f m\r\n", dist);
	else
		uart_puts("TIMEOUT/ERR\r\n");
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
	case CMD_INITIALIZE:   handle_initialize();  break;
	case CMD_RECONFIGURE:  handle_reconfigure(); break;
	case CMD_RESET:        handle_reset();       break;
	case CMD_GET_STATUS:   handle_get_status();  break;
	case CMD_DEBUG_ON:     handle_debug(1);      break;
	case CMD_DEBUG_OFF:    handle_debug(0);      break;
	case CMD_TEST_SS_TWR:  handle_test_ss_twr(); break;
	default:
		uart_printf("Command not implemented: %s\r\n", cmd_str(cmd.code));
		break;
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

	uart_puts("\r\n========================================\r\n");
	uart_puts("Main Anchor Station\r\n");
	uart_puts("========================================\r\n");
	uart_puts("Commands: INITIALIZE, RECONFIGURE, RESET, GET_STATUS, DEBUG_ON/OFF, TEST_SS_TWR\r\n");
	uart_puts("> ");
}

void main_anchor_loop(void)
{
	/* Drain any pending RX frames (enumeration/config/TWR handled automatically) */
	net_process(&devices, NULL);

	/* Block until the user sends a command over UART */
	static char line_buf[128];
	uart_readline(line_buf, sizeof(line_buf));
	process_command(cmd_parse(line_buf));
	uart_puts("\r\n> ");
}
