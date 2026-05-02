#include "main_anchor.h"
#include "ss_twr.h"
#include "uart.h"
#include "deca_device_api.h"
#include <string.h>
#include <ctype.h>

#define TX_ANT_DLY 16724
#define RX_ANT_DLY 16724

void main_anchor_init(void)
{
	uart_init(115200);
	uart_puts("\r\n=== SS-TWR INITIATOR ===\r\n");
	uart_puts("Commands: TEST_SS_TWR\r\n> ");

	dwt_setrxantennadelay(RX_ANT_DLY);
	dwt_settxantennadelay(TX_ANT_DLY);
	dwt_enableframefilter(DWT_FF_NOTYPE_EN);
}

void main_anchor_loop(void)
{
	static char line_buf[32];
	uart_readline(line_buf, sizeof(line_buf));

	char upper[32];
	for (int i = 0; i < 31 && line_buf[i]; i++)
		upper[i] = toupper((unsigned char)line_buf[i]);
	upper[31] = '\0';

	if (strcmp(upper, "TEST_SS_TWR") == 0) {
		float dist;
		if (ss_twr_measure_distance(2, &dist) == 0)
			uart_printf("dist=%.2f m\r\n", dist);
		else
			uart_puts("TIMEOUT/ERR\r\n");
	} else {
		uart_puts("Unknown command\r\n");
	}
	uart_puts("> ");
}
