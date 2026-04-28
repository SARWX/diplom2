#include "net_mac.h"
#include "ss_twr.h"
#include "deca_device_api.h"
#include "deca_regs.h"

#define UUS_TO_DWT_TIME 65536
#define SPEED_OF_LIGHT 299702547
#define TX_ANT_DLY 16436
#define RX_ANT_DLY 16436
#define POLL_TX_TO_RESP_RX_DLY_UUS 140
#define RESP_RX_TIMEOUT_UUS 210
#define POLL_RX_TO_RESP_TX_DLY_UUS 330

static uint8_t twr_frame_seq = 0;
static float twr_last_distance = 0;

static int twr_send_poll(net_addr16_t dst_addr)
{
	uint8_t poll_msg[12];
	uint16_t len;
	
	/* Build frame with function code 0xE0 */
	len = net_build_frame(poll_msg, NULL, dst_addr, twr_frame_seq++, 
						(const uint8_t*)"\xE0", 1);
	return net_send_frame(poll_msg, len, 1);
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
	uint32 status_reg;
	uint32 poll_tx_ts, resp_rx_ts, poll_rx_ts = 0, resp_tx_ts = 0;
	int ret = -1;

	/* Enter critical section - disables interrupts */
	irq_state = decamutexon();

	/* Send poll with response expected */
	if (twr_send_poll(dst_addr) < 0)
		goto restore;

	/* Wait for response */
	while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & 
		(SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
		; /* Wait */
	}

	if (status_reg & SYS_STATUS_RXFCG) {
		uint16_t len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
		if (len <= sizeof(net_state.rx_buffer)) {
			dwt_readrxdata(net_state.rx_buffer, len, 0);
			
			poll_tx_ts = dwt_readtxtimestamplo32();
			resp_rx_ts = dwt_readrxtimestamplo32();
			
			/* Extract timestamps from response (bytes 10-17) */
			for (int i = 0; i < 4; i++) {
				poll_rx_ts |= net_state.rx_buffer[10 + i] << (i * 8);
				resp_tx_ts |= net_state.rx_buffer[14 + i] << (i * 8);
			}
			
			*distance = twr_calc_distance(poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts);
			twr_last_distance = *distance;
			ret = 0;
		}
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
	} else {
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
	}

restore:
	/* Exit critical section - restores interrupts */
	decamutexoff(irq_state);
	return ret;
}

int ss_twr_handle_rx_frame(const net_message_t* msg)
{
	/* Check if it's a poll frame (function code 0xE0) */
	if (msg->payload_len >= 1 && msg->payload[0] == SS_TWR_FUNC_POLL) {
		uint64_t poll_rx_ts = 0;
		uint8_t ts_tab[5];
		uint8_t resp_msg[20];
		uint32 resp_tx_time;
		
		/* Get poll reception timestamp */
		dwt_readrxtimestamp(ts_tab);
		for (int i = 4; i >= 0; i--) {
			poll_rx_ts <<= 8;
			poll_rx_ts |= ts_tab[i];
		}
		
		/* Build response with function code 0xE1 */
		uint16_t len = net_build_frame(resp_msg, NULL, msg->src_addr16,
					twr_frame_seq++, (const uint8_t*)"\xE1", 1);
		
		/* Add timestamps after header */
		for (int i = 0; i < 4; i++) {
			resp_msg[10 + i] = (poll_rx_ts >> (i * 8)) & 0xFF;
		}
		
		/* Delayed transmission */
		resp_tx_time = (poll_rx_ts + (POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME)) >> 8;
		dwt_setdelayedtrxtime(resp_tx_time);
		
		dwt_writetxdata(len + 8, resp_msg, 0);  /* +8 for timestamps */
		dwt_writetxfctrl(len + 8, 0, 1);
		dwt_starttx(DWT_START_TX_DELAYED);
		return 1;
	}
	return 0;
}
