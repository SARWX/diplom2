#include "ss_twr.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "uart.h"
#include <string.h>

#define UUS_TO_DWT_TIME            65536
#define SPEED_OF_LIGHT             299702547
#define TX_ANT_DLY                 16724
#define POLL_TX_TO_RESP_RX_DLY_UUS 140
#define RESP_RX_TIMEOUT_UUS        3000
#define POLL_RX_TO_RESP_TX_DLY_UUS 2000

/* Verbatim Decawave ex_06a/b frame format */
static uint8_t tx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};
static uint8_t rx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8_t tx_resp_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'V', 'E', 'W', 'A', 0xE1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0};
static uint8_t rx_poll_msg[] = {0x41, 0x88, 0, 0xCA, 0xDE, 'W', 'A', 'V', 'E', 0xE0, 0, 0};

#define ALL_MSG_COMMON_LEN      10
#define ALL_MSG_SN_IDX          2
#define RESP_MSG_POLL_RX_TS_IDX 10
#define RESP_MSG_RESP_TX_TS_IDX 14
#define RESP_MSG_TS_LEN         4
#define POLL_RX_BUF_LEN         12
#define RESP_RX_BUF_LEN         20

static uint8_t frame_seq_nb = 0;
static uint8_t rx_buffer[RESP_RX_BUF_LEN];

static void ts_get(const uint8_t *field, uint32_t *ts)
{
	*ts = 0;
	for (int i = 0; i < RESP_MSG_TS_LEN; i++)
		*ts += field[i] << (i * 8);
}

static uint64_t get_rx_timestamp_u64(void)
{
	uint8_t ts_tab[5];
	uint64_t ts = 0;
	dwt_readrxtimestamp(ts_tab);
	for (int i = 4; i >= 0; i--) {
		ts <<= 8;
		ts |= ts_tab[i];
	}
	return ts;
}

static void ts_set(uint8_t *field, uint64_t ts)
{
	for (int i = 0; i < RESP_MSG_TS_LEN; i++)
		field[i] = (ts >> (i * 8)) & 0xFF;
}

/*
 * Initiator: send POLL, wait for RESP, compute distance.
 * Returns 0 on success; -1 on timeout/error/mismatch.
 */
int ss_twr_measure_distance(float *distance)
{
	uint32_t status_reg;

	tx_poll_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
	dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
	dwt_writetxdata(sizeof(tx_poll_msg), tx_poll_msg, 0);
	dwt_writetxfctrl(sizeof(tx_poll_msg), 0, 1);
	dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
	dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
	dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

	while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
		 (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)))
		;

	frame_seq_nb++;

	if (!(status_reg & SYS_STATUS_RXFCG)) {
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
		dwt_rxreset();
		return -1;
	}

	dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
	uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFLEN_MASK;
	if (frame_len <= RESP_RX_BUF_LEN)
		dwt_readrxdata(rx_buffer, frame_len, 0);

	rx_buffer[ALL_MSG_SN_IDX] = 0;
	if (memcmp(rx_buffer, rx_resp_msg, ALL_MSG_COMMON_LEN) != 0)
		return -1;

	uint32_t poll_tx_ts = dwt_readtxtimestamplo32();
	uint32_t resp_rx_ts = dwt_readrxtimestamplo32();

	uint32_t poll_rx_ts, resp_tx_ts;
	ts_get(&rx_buffer[RESP_MSG_POLL_RX_TS_IDX], &poll_rx_ts);
	ts_get(&rx_buffer[RESP_MSG_RESP_TX_TS_IDX], &resp_tx_ts);

	int32_t rtd_init = resp_rx_ts - poll_tx_ts;
	int32_t rtd_resp = resp_tx_ts - poll_rx_ts;
	double tof = ((rtd_init - rtd_resp) / 2.0) * DWT_TIME_UNITS;
	*distance = (float)(tof * SPEED_OF_LIGHT);

	uart_printf("  poll_tx=%u resp_rx=%u\r\n", poll_tx_ts, resp_rx_ts);
	uart_printf("  poll_rx=%u resp_tx=%u\r\n", poll_rx_ts, resp_tx_ts);
	uart_printf("  rtd_init=%d rtd_resp=%d\r\n", (int)rtd_init, (int)rtd_resp);
	return 0;
}

/*
 * Responder: call after receiving a frame.
 * Returns 1 if a POLL was recognised and RESP sent; 0 otherwise.
 */
int ss_twr_handle_poll(const uint8_t *frame, uint16_t frame_len)
{
	uint8_t local[POLL_RX_BUF_LEN];
	if (frame_len > POLL_RX_BUF_LEN)
		return 0;
	memcpy(local, frame, frame_len);
	local[ALL_MSG_SN_IDX] = 0;
	if (memcmp(local, rx_poll_msg, ALL_MSG_COMMON_LEN) != 0)
		return 0;

	uint64_t poll_rx_ts  = get_rx_timestamp_u64();
	uint32_t resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
	uint64_t resp_tx_ts  = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

	tx_resp_msg[ALL_MSG_SN_IDX] = frame_seq_nb;
	ts_set(&tx_resp_msg[RESP_MSG_POLL_RX_TS_IDX], poll_rx_ts);
	ts_set(&tx_resp_msg[RESP_MSG_RESP_TX_TS_IDX], resp_tx_ts);

	dwt_setdelayedtrxtime(resp_tx_time);
	dwt_writetxdata(sizeof(tx_resp_msg), tx_resp_msg, 0);
	dwt_writetxfctrl(sizeof(tx_resp_msg), 0, 1);

	if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
		while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS))
			;
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
		frame_seq_nb++;
		return 1;
	}
	return 0;
}
