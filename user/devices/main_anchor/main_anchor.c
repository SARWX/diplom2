#include "main_anchor.h"
#include "net_devices.h"
#include "net_dispatch.h"
#include "enumeration.h"
#include "ss_twr.h"
#include "uart.h"
#include "cmd_parser.h"
#include "deca_device_api.h"
#include <string.h>
#include <ctype.h>

#define TX_ANT_DLY 16724
#define RX_ANT_DLY 16724

static net_devices_list_t devices;

void main_anchor_init(void)
{
	uart_init(115200);
	uart_puts("\r\n=== SS-TWR INITIATOR ===\r\n");
	uart_puts("Commands: INITIALIZE, TEST_SS_TWR\r\n> ");

	net_radio_init();
	dwt_setrxantennadelay(RX_ANT_DLY);
	dwt_settxantennadelay(TX_ANT_DLY);
	dwt_enableframefilter(DWT_FF_NOTYPE_EN);

	net_devices_init(&devices);
}

void main_anchor_loop(void)
{
	static char line_buf[64];
	uart_readline(line_buf, sizeof(line_buf));

	cmd_parse_result_t cmd = cmd_parse(line_buf);

	switch (cmd.code) {
	case CMD_INITIALIZE:
		uart_puts("\r\n>>> INITIALIZE\r\n");
		net_devices_clear(&devices);
		net_devices_init(&devices);
		enumeration_start_master(&devices);
		break;

	case CMD_TEST_SS_TWR:
		uart_puts("\r\n>>> TEST_SS_TWR\r\n");
		{
			float dist;
			int ok = ss_twr_measure_distance(2, &dist);
			if (ok == 0)
				uart_printf("dist=%.2f m\r\n", dist);
			else
				uart_puts("TIMEOUT/ERR\r\n");
		}
		break;

	default:
		if (cmd.valid)
			uart_puts("Command not implemented\r\n");
		else if (line_buf[0] != '\0')
			uart_puts("Unknown command\r\n");
		break;
	}

	uart_puts("> ");
}
