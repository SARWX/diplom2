/*
 * SS-TWR Initiator — ex_06a_ss_twr_init ported verbatim.
 * LCD replaced with UART.
 */

#include "main_anchor.h"
#include "uart.h"
#include "sleep.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include <string.h>
#include <ctype.h>

static dwt_config_t config = {
	2,               /* Channel number. */
	DWT_PRF_64M,     /* Pulse repetition frequency. */
	DWT_PLEN_128,    /* Preamble length. Used in TX only. */
	DWT_PAC8,        /* Preamble acquisition chunk size. Used in RX only. */
	9,               /* TX preamble code. Used in TX only. */
	9,               /* RX preamble code. Used in RX only. */
	0,               /* 0 to use standard SFD, 1 to use non-standard SFD. */
	DWT_BR_6M8,      /* Data rate. */
	DWT_PHRMODE_STD, /* PHY header mode. */
	(129 + 8 - 8)    /* SFD timeout (preamble length + 1 + SFD length - PAC size). */
};

#define TX_ANT_DLY 16436
#define RX_ANT_DLY 16436

static uint8 tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
static uint8 rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define ALL_MSG_COMMON_LEN      10
#define ALL_MSG_SN_IDX          2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN         4
#define RX_BUF_LEN              20

#define UUS_TO_DWT_TIME            65536
#define POLL_TX_TO_RESP_RX_DLY_UUS 140
#define RESP_RX_TIMEOUT_UUS        3000
#define SPEED_OF_LIGHT             299702547

static uint8 frame_seq_nb = 0;
static uint8 rx_buffer[RX_BUF_LEN];
static uint32 status_reg = 0;

static double tof;
static double distance;

static void resp_msg_get_ts(uint8 *ts_field, uint32 *ts)
{
	int i;
	*ts = 0;
	for (i = 0; i < RESP_MSG_TS_LEN; i++)
		*ts += ts_field[i] << (i * 8);
}

static void do_measure(void)
{
	tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
	dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
	dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
	dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);
	dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

	while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
		 (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)))
		;

	frame_seq_nb++;

	if (status_reg & SYS_STATUS_RXFCG) {
		uint32 frame_len;
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);

		frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFLEN_MASK;
		if (frame_len <= RX_BUF_LEN)
			dwt_readrxdata(rx_buffer, frame_len, 0);

		rx_buffer[ALL_MSG_SN_IDX] = 0;
		if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) == 0) {
			uint32 poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts;
			int32 rtd_init, rtd_resp;
			float clockOffsetRatio;

			poll_tx_ts = dwt_readtxtimestamplo32();
			resp_rx_ts = dwt_readrxtimestamplo32();

			clockOffsetRatio = dwt_readcarrierintegrator() * 0.945e-6f;

			resp_msg_get_ts(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
			resp_msg_get_ts(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

			rtd_init = resp_rx_ts - poll_tx_ts;
			rtd_resp = resp_tx_ts - poll_rx_ts;

			tof = ((rtd_init - rtd_resp * (1 - clockOffsetRatio)) / 2.0) * DWT_TIME_UNITS;
			distance = tof * SPEED_OF_LIGHT;

			uart_printf("dist=%.2f m\r\n", (float)distance);
		} else {
			uart_puts("RX mismatch\r\n");
		}
	} else {
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
		dwt_rxreset();
		uart_printf("TIMEOUT/ERR status=0x%08X\r\n", status_reg);
	}
}

void main_anchor_init(void)
{
	uart_init(115200);
	uart_puts("\r\n=== SS-TWR INITIATOR ===\r\n");
	uart_puts("Commands: TEST_SS_TWR\r\n> ");

	dwt_configure(&config);
	dwt_setrxantennadelay(RX_ANT_DLY);
	dwt_settxantennadelay(TX_ANT_DLY);
	dwt_enableframefilter(DWT_FF_NOTYPE_EN);
	dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
	dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
}

void main_anchor_loop(void)
{
	static char line_buf[32];
	uart_readline(line_buf, sizeof(line_buf));

	/* uppercase compare */
	char upper[32];
	for (int i = 0; i < 31 && line_buf[i]; i++)
		upper[i] = toupper((unsigned char)line_buf[i]);
	upper[31] = '\0';

	if (strcmp(upper, "TEST_SS_TWR") == 0) {
		do_measure();
	} else {
		uart_puts("Unknown command\r\n");
	}
	uart_puts("> ");
}
