#include "net_mac.h"
#include "ss_twr.h"
#include "deca_device_api.h"
#include "deca_regs.h"

#define UUS_TO_DWT_TIME 65536
#define SPEED_OF_LIGHT 299702547
#define TX_ANT_DLY 16436
#define RX_ANT_DLY 16436
#define POLL_TX_TO_RESP_RX_DLY_UUS 140
#define RESP_RX_TIMEOUT_UUS 2100
#define POLL_RX_TO_RESP_TX_DLY_UUS 330

static uint8_t twr_frame_seq = 0;
static float twr_last_distance = 0;

static int twr_send_poll(net_addr16_t dst_addr)
{
	uint8_t poll_msg[12];
	uint16_t len;
	
	len = net_build_frame(poll_msg, NULL, dst_addr, twr_frame_seq++, 
				(const uint8_t*)"\xE0", 1);
	return net_send_frame_ranging(poll_msg, len, 1);
}

static float twr_calc_distance(uint32 poll_tx_ts, uint32 resp_rx_ts, 
				uint32 poll_rx_ts, uint32 resp_tx_ts)
{
	int32 rtd_init = resp_rx_ts - poll_tx_ts;
	int32 rtd_resp = resp_tx_ts - poll_rx_ts;
	float clock_offset = dwt_readcarrierintegrator() * 0.945e-6f;
	double tof = ((rtd_init - rtd_resp * (1 - clock_offset)) / 2.0) * DWT_TIME_UNITS;
	return (float)(tof * SPEED_OF_LIGHT);
}

int ss_twr_measure_distance(net_addr16_t dst_addr, float* distance)
{
	decaIrqStatus_t irq_state;
	uint32 poll_tx_ts, resp_rx_ts, poll_rx_ts = 0, resp_tx_ts = 0;
	int ret = -1;

	irq_state = decamutexon();

	dwt_forcetrxoff();
	dwt_rxreset();

	/* Set TWR-specific timeouts */
	dwt_setrxaftertxdelay(POLL_TX_TO_RESP_RX_DLY_UUS);
	dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);

	if (twr_send_poll(dst_addr) != 1)
		goto restore;

	poll_tx_ts = dwt_readtxtimestamplo32();
	resp_rx_ts = dwt_readrxtimestamplo32();

	for (int i = 0; i < 4; i++) {
		poll_rx_ts |= (uint32)net_state.rx_buffer[10 + i] << (i * 8);
		resp_tx_ts |= (uint32)net_state.rx_buffer[14 + i] << (i * 8);
	}

	*distance = twr_calc_distance(poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts);
	ret = 0;

restore:
	/* Restore to safe defaults (no timeout, no extra delay) */
	dwt_setrxaftertxdelay(0);
	dwt_setrxtimeout(0);
	decamutexoff(irq_state);
	return ret;
}

int ss_twr_handle_rx_frame(const net_message_t* msg)
{
	if (msg->payload_len >= 1 && msg->payload[0] == SS_TWR_FUNC_POLL) {
		uint64_t poll_rx_ts = 0;
		uint8_t ts_tab[5];
		uint8_t resp_msg[20];
		uint32 resp_tx_time;
		uint32_t resp_tx_ts_est;

		dwt_readrxtimestamp(ts_tab);
		for (int i = 4; i >= 0; i--) {
			poll_rx_ts <<= 8;
			poll_rx_ts |= ts_tab[i];
		}

		uint16_t len = net_build_frame(resp_msg, NULL, msg->src_addr16,
					twr_frame_seq++, (const uint8_t*)"\xE1", 1);

		resp_tx_time = (uint32)((poll_rx_ts +
			(uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME) >> 8);
		resp_tx_ts_est = (resp_tx_time << 8) + TX_ANT_DLY;

		for (int i = 0; i < 4; i++) {
			resp_msg[10 + i] = (uint8_t)((uint32_t)(poll_rx_ts >> (i * 8)) & 0xFF);
			resp_msg[14 + i] = (uint8_t)((resp_tx_ts_est >> (i * 8)) & 0xFF);
		}

		dwt_setdelayedtrxtime(resp_tx_time);
		dwt_writetxdata(len + 8, resp_msg, 0);
		dwt_writetxfctrl(len + 8, 0, 1);
		dwt_starttx(DWT_START_TX_DELAYED);

		while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS))
			;
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
		return 1;
	}
	return 0;
}

/* Called from net_rx_ok_isr for time-critical TWR POLL frames.
 * Sends RESPONSE via delayed TX and re-arms RX before returning.
 * Busy-waits ~500 µs for TX completion — acceptable because the radio
 * is half-duplex and no frames can arrive during that window. */
void ss_twr_isr(const uint8_t *frame, uint16_t len)
{
	uint64_t poll_rx_ts = 0;
	uint8_t ts_tab[5];
	uint8_t resp_msg[20];
	uint32 resp_tx_time;
	uint32_t resp_tx_ts_est;

	dwt_readrxtimestamp(ts_tab);
	for (int i = 4; i >= 0; i--) {
		poll_rx_ts <<= 8;
		poll_rx_ts |= ts_tab[i];
	}

	/* src address at bytes [7-8] in 16-bit MAC header */
	net_addr16_t src_addr = frame[7] | ((net_addr16_t)frame[8] << 8);

	uint16_t resp_len = net_build_frame(resp_msg, NULL, src_addr,
				twr_frame_seq++, (const uint8_t*)"\xE1", 1);

	resp_tx_time = (uint32)((poll_rx_ts +
		(uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME) >> 8);
	resp_tx_ts_est = (resp_tx_time << 8) + TX_ANT_DLY;

	for (int i = 0; i < 4; i++) {
		resp_msg[10 + i] = (uint8_t)((uint32_t)(poll_rx_ts >> (i * 8)) & 0xFF);
		resp_msg[14 + i] = (uint8_t)((resp_tx_ts_est >> (i * 8)) & 0xFF);
	}

	dwt_setdelayedtrxtime(resp_tx_time);
	dwt_writetxdata(resp_len + 8, resp_msg, 0);
	dwt_writetxfctrl(resp_len + 8, 0, 1);
	dwt_starttx(DWT_START_TX_DELAYED);

	while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS))
		;
	dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}
