/*
 * SS-TWR Responder — ex_06b_ss_twr_resp ported verbatim.
 * LCD replaced with nothing (debug counters only).
 */

#include "anchor.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include <string.h>

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

#define TX_ANT_DLY 16724
#define RX_ANT_DLY 16724

static uint8 rx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
static uint8 tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};

#define ALL_MSG_COMMON_LEN      10
#define ALL_MSG_SN_IDX          2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN         4
#define RX_BUF_LEN              12

#define UUS_TO_DWT_TIME            65536
#define POLL_RX_TO_RESP_TX_DLY_UUS 2000

static uint8 frame_seq_nb = 0;
static uint8 rx_buffer[RX_BUF_LEN];
static uint32 status_reg = 0;

typedef unsigned long long uint64;
static uint64 poll_rx_ts;
static uint64 resp_tx_ts;

/* Debug counters */
uint32 poll_count    = 0;
uint32 resp_count    = 0;
uint32 tx_fail_count = 0;
uint32 rx_err_count  = 0;

static uint64 get_rx_timestamp_u64(void)
{
	uint8 ts_tab[5];
	uint64 ts = 0;
	int i;
	dwt_readrxtimestamp(ts_tab);
	for (i = 4; i >= 0; i--) {
		ts <<= 8;
		ts |= ts_tab[i];
	}
	return ts;
}

static void resp_msg_set_ts(uint8 *ts_field, const uint64 ts)
{
	int i;
	for (i = 0; i < RESP_MSG_TS_LEN; i++)
		ts_field[i] = (ts >> (i * 8)) & 0xFF;
}

void anchor_init(void)
{
	dwt_configure(&config);
	dwt_setrxantennadelay(RX_ANT_DLY);
	dwt_settxantennadelay(TX_ANT_DLY);
	dwt_enableframefilter(DWT_FF_NOTYPE_EN);
}

void anchor_loop(void)
{
	uint32 frame_len;
	int ret;

	dwt_rxenable(DWT_START_RX_IMMEDIATE);

	while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
		 (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_ERR)))
		;

	if (status_reg & SYS_STATUS_RXFCG) {
		uint32 resp_tx_time;

		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);

		frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
		if (frame_len <= RX_BUF_LEN)
			dwt_readrxdata(rx_buffer, frame_len, 0);

		rx_buffer[ALL_MSG_SN_IDX] = 0;
		if (memcmp(rx_buffer, rx_poll_msg, ALL_MSG_COMMON_LEN) != 0)
			return;

		poll_count++;

		poll_rx_ts = get_rx_timestamp_u64();

		resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
		dwt_setdelayedtrxtime(resp_tx_time);

		resp_tx_ts = (((uint64)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

		resp_msg_set_ts(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
		resp_msg_set_ts(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);

		tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
		dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
		dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);
		ret = dwt_starttx(DWT_START_TX_DELAYED);

		if (ret == DWT_SUCCESS) {
			while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS))
				;
			dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
			frame_seq_nb++;
			resp_count++;
		} else {
			tx_fail_count++;
		}
	} else {
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
		dwt_rxreset();
		rx_err_count++;
	}
}
