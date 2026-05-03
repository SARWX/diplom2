#include "ss_twr.h"
#include "net_mac.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "uart.h"

#define UUS_TO_DWT_TIME            65536
#define SPEED_OF_LIGHT             299702547
#define TX_ANT_DLY                 16724
#define POLL_TX_TO_RESP_RX_DLY_UUS 140
#define RESP_RX_TIMEOUT_UUS        3000
#define POLL_RX_TO_RESP_TX_DLY_UUS 2000

#define SS_TWR_FUNC_RESP 0xE1

/*
 * Frame layout (net_mac 16-bit header = 9 bytes):
 *  [0-1] FC  [2] seq  [3-4] PAN  [5-6] dst  [7-8] src
 *  [9]   func_code
 *  [10-13]   poll_rx_ts   (RESP only)
 *  [14-17]   resp_tx_ts   (RESP only)
 *  [18-19]   FCS (DW1000 fills)
 */
#define FUNC_CODE_IDX  9
#define POLL_RX_TS_IDX 10
#define RESP_TX_TS_IDX 14
#define RESP_RX_BUF_LEN 20

static uint8_t frame_seq_nb = 0;
static uint8_t rx_buffer[RESP_RX_BUF_LEN];

static void ts_get(const uint8_t *field, uint32_t *ts)
{
	*ts = 0;
	for (int i = 0; i < 4; i++)
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
	for (int i = 0; i < 4; i++)
		field[i] = (ts >> (i * 8)) & 0xFF;
}

/*
 * Initiator: send POLL to dst_addr, wait for RESP, compute distance.
 * Returns 0 on success; -1 on timeout/error/mismatch.
 */
int ss_twr_measure_distance(net_addr16_t dst_addr, float *distance)
{
	uint8_t poll_msg[12];
	uint32_t status_reg;
	uint8_t poll_payload = SS_TWR_FUNC_POLL;
	int ret = -1;

	uint16_t len = net_build_frame(poll_msg, NULL, dst_addr, frame_seq_nb++,
				       &poll_payload, 1);

	decaIrqStatus_t irq = decamutexon();
	dwt_forcetrxoff();
	dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
	dwt_writetxdata(len, poll_msg, 0);
	dwt_writetxfctrl(len, 0, 1);
	dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
	dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);
	dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);

	while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
		 (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)))
		;

	decamutexoff(irq);
	dwt_rxenable(DWT_START_RX_IMMEDIATE);

	if (!(status_reg & SYS_STATUS_RXFCG)) {
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
		dwt_rxreset();
		goto out;
	}

	dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
	uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFLEN_MASK;
	if (frame_len <= RESP_RX_BUF_LEN)
		dwt_readrxdata(rx_buffer, frame_len, 0);

	if (frame_len < FUNC_CODE_IDX + 1 || rx_buffer[FUNC_CODE_IDX] != SS_TWR_FUNC_RESP)
		goto out;

	{
		uint32_t poll_tx_ts = dwt_readtxtimestamplo32();
		uint32_t resp_rx_ts = dwt_readrxtimestamplo32();
		uint32_t poll_rx_ts, resp_tx_ts;
		ts_get(&rx_buffer[POLL_RX_TS_IDX], &poll_rx_ts);
		ts_get(&rx_buffer[RESP_TX_TS_IDX], &resp_tx_ts);

		int32_t rtd_init = resp_rx_ts - poll_tx_ts;
		int32_t rtd_resp = resp_tx_ts - poll_rx_ts;
		double tof = ((rtd_init - rtd_resp) / 2.0) * DWT_TIME_UNITS;
		*distance = (float)(tof * SPEED_OF_LIGHT);

		uart_printf("  poll_tx=%u resp_rx=%u\r\n", poll_tx_ts, resp_rx_ts);
		uart_printf("  poll_rx=%u resp_tx=%u\r\n", poll_rx_ts, resp_tx_ts);
		uart_printf("  rtd_init=%d rtd_resp=%d\r\n", (int)rtd_init, (int)rtd_resp);
		ret = 0;
	}

out:
	return ret;
}

/*
 * Responder: called from ISR when a POLL frame arrives.
 * Extracts src_addr from POLL header, sends RESP with proper addressing.
 * Returns 1 if RESP sent; 0 otherwise.
 */
int ss_twr_handle_poll(const uint8_t *frame, uint16_t frame_len)
{
	if (frame_len < FUNC_CODE_IDX + 1 || frame[FUNC_CODE_IDX] != SS_TWR_FUNC_POLL)
		return 0;

	/* src_addr is at bytes [7-8] in 16-bit MAC header */
	net_addr16_t src_addr = frame[7] | ((net_addr16_t)frame[8] << 8);

	uint64_t poll_rx_ts   = get_rx_timestamp_u64();
	uint32_t resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
	uint64_t resp_tx_ts   = (((uint64_t)(resp_tx_time & 0xFFFFFFFEUL)) << 8) + TX_ANT_DLY;

	uint8_t payload[9];
	payload[0] = SS_TWR_FUNC_RESP;
	ts_set(&payload[1], poll_rx_ts);
	ts_set(&payload[5], resp_tx_ts);

	uint8_t resp_msg[20];
	uint16_t len = net_build_frame(resp_msg, NULL, src_addr, frame_seq_nb++, payload, 9);

	dwt_setdelayedtrxtime(resp_tx_time);
	dwt_writetxdata(len, resp_msg, 0);
	dwt_writetxfctrl(len, 0, 1);

	if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
		while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS))
			;
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
		return 1;
	}
	return 0;
}
