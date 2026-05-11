#include "net_mac.h"
#include "ss_twr.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "port.h"

/* Iteration-count watchdog for TXFRS inside ISR (no SysTick there).
 * DW1000 at 6.8 Mbps finishes a 20-byte frame in ~300 µs.
 * At 72 MHz with ~4 cycles/iter this gives ~60 000 iters budget;
 * 200 000 is a safe 10× margin before we declare a TX failure. */
#define ISR_TX_TIMEOUT_ITERS 200000u
#define MAIN_TX_TIMEOUT_MS   10u

#define UUS_TO_DWT_TIME 65536
#define SPEED_OF_LIGHT 299702547
/* DWT_TIME_UNITS as float to avoid pulling in double-precision library */
#define DWT_TIME_UNITS_F 1.5650040064103e-11f
#define TX_ANT_DLY 16436
#define RX_ANT_DLY 16436

/* Carrier integrator clock correction (DecaWave AN005 §3).
 * HERTZ_TO_PPM is negative so clock_offset_ratio is negative when the
 * initiator is faster than the responder — this makes the formula
 * (rtd_init - rtd_resp*(1+ratio)) equivalent to the example's
 * (rtd_init - rtd_resp*(1-positive_ratio)).
 * Constants are defined in deca_device_api.h (double precision there);
 * cast to float at the use site to avoid pulling in double library. */

/* Default antenna delays and temperature coefficient — overridable at runtime. */
#define K_TEMP_DEFAULT    0.0f
#define T_REF             23.0f

static uint16_t g_tx_ant_dly = TX_ANT_DLY;
static uint16_t g_rx_ant_dly = RX_ANT_DLY;
static float    g_k_temp     = K_TEMP_DEFAULT;
static float    g_last_temp  = T_REF;

/* At 6.8 Mbps, RMARKER→RXFCG ≈ 300 µs for a 12-byte frame.
 * Responder delay 3500 µs gives ~3200 µs ISR budget.
 * Initiator enables RX explicitly after TXFRS (~20 µs); response
 * arrives ~3200 µs later, well inside the 5000 µs timeout. */
#define RESP_RX_TIMEOUT_UUS        5000
#define POLL_RX_TO_RESP_TX_DLY_UUS 2000

static uint8_t twr_frame_seq = 0;

/*
 * Pre-built response frame (20 bytes).  Fixed fields are filled by
 * ss_twr_resp_init(); the ISR only writes seq_num, dest_addr and the
 * two timestamps — no net_build_frame() call in the hot path.
 *
 * Layout (16-bit → 16-bit frame, PAN compressed):
 *  [0-1]  FC
 *  [2]    seq_num         ← written per call
 *  [3-4]  PAN_ID
 *  [5-6]  dest_addr       ← written per call
 *  [7-8]  src_addr (own)
 *  [9]    func code 0xE1
 *  [10-13] poll_rx_ts     ← written per call
 *  [14-17] resp_tx_ts_est ← written per call
 *  [18-19] FCS (DW1000 fills)
 */
static uint8_t resp_template[20];

void ss_twr_resp_init(void)
{
	uint16_t fc = net_build_frame_control(NET_ADDR_MODE_16BIT, NET_ADDR_MODE_16BIT, 1, 0);
	net_addr16_t my_addr = net_get_src_addr16();
	resp_template[0] = fc & 0xFF;
	resp_template[1] = (fc >> 8) & 0xFF;
	resp_template[3] = NET_PAN_ID & 0xFF;
	resp_template[4] = (NET_PAN_ID >> 8) & 0xFF;
	resp_template[7] = my_addr & 0xFF;
	resp_template[8] = (my_addr >> 8) & 0xFF;
	resp_template[9] = SS_TWR_FUNC_RESP;
}

static float twr_calc_distance(uint32 poll_tx_ts, uint32 resp_rx_ts,
				uint32 poll_rx_ts, uint32 resp_tx_ts,
				float clock_offset_ratio)
{
	int32 rtd_init = resp_rx_ts - poll_tx_ts;
	int32 rtd_resp = resp_tx_ts - poll_rx_ts;
	float rtd_resp_corr = (float)rtd_resp * (1.0f + clock_offset_ratio);
	float tof = ((float)rtd_init - rtd_resp_corr) * 0.5f * DWT_TIME_UNITS_F;
	return tof * (float)SPEED_OF_LIGHT;
}

int ss_twr_measure_distance(net_addr16_t dst_addr, float* distance, float* temperature)
{
    decaIrqStatus_t irq_state;
    uint32_t poll_tx_ts, resp_rx_ts, poll_rx_ts = 0, resp_tx_ts = 0;
    uint32_t status_reg;
    int ret = -1;
    uint8_t poll_msg[12];
    uint16_t len;

    irq_state = decamutexon();

    len = net_build_frame(poll_msg, NULL, dst_addr, twr_frame_seq++,
                          (const uint8_t*)"\xE0", 1);

    dwt_forcetrxoff();
    dwt_rxreset();

    /* Configure RX timeout and delay before enabling */
    dwt_setrxaftertxdelay(0);
    dwt_setrxtimeout(RESP_RX_TIMEOUT_UUS);

    dwt_writetxdata(len, poll_msg, 0);
    dwt_writetxfctrl(len, 0, 1);

    /* Start TX with automatic RX enable */
    if (dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED) != DWT_SUCCESS)
        goto restore;

    /* Wait for response or timeout */
    while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
             (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR)));

    if (!(status_reg & SYS_STATUS_RXFCG)) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
        goto restore;
    }

    /* Response received — read timestamps and clear flags */
    poll_tx_ts = dwt_readtxtimestamplo32();
    resp_rx_ts = dwt_readrxtimestamplo32();
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG | SYS_STATUS_TXFRS);

    /* Read response data */
    uint16_t rx_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
    if (rx_len <= sizeof(net_state.rx_buffer))
        dwt_readrxdata(net_state.rx_buffer, rx_len, 0);

    for (int i = 0; i < 4; i++) {
        poll_rx_ts |= (uint32)net_state.rx_buffer[10 + i] << (i * 8);
        resp_tx_ts |= (uint32)net_state.rx_buffer[14 + i] << (i * 8);
    }

    {
        int32 carrier_int = dwt_readcarrierintegrator();
        float clock_offset = (float)carrier_int *
                             (float)(FREQ_OFFSET_MULTIPLIER * HERTZ_TO_PPM_MULTIPLIER_CHAN_2 / 1.0e6);
        *distance = twr_calc_distance(poll_tx_ts, resp_rx_ts, poll_rx_ts, resp_tx_ts,
                                      clock_offset) - g_k_temp * (g_last_temp - T_REF);
        if (temperature)
            *temperature = g_last_temp;
    }
    ret = 0;

restore:
    dwt_setrxtimeout(0);
    decamutexoff(irq_state);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    return ret;
}

int ss_twr_handle_rx_frame(const net_message_t* msg)
{
	if (msg->payload_len >= 1 && msg->payload[0] == SS_TWR_FUNC_POLL) {
		uint64_t poll_rx_ts = 0;
		uint8_t ts_tab[5];
		uint32 resp_tx_time;
		uint32_t resp_tx_ts_est;

		dwt_readrxtimestamp(ts_tab);
		for (int i = 4; i >= 0; i--) {
			poll_rx_ts <<= 8;
			poll_rx_ts |= ts_tab[i];
		}

		resp_tx_time = (uint32)((poll_rx_ts +
			(uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME) >> 8);
		resp_tx_time &= 0xFFFFFFFEUL;
		resp_tx_ts_est = (resp_tx_time << 8) + g_tx_ant_dly;

		resp_template[2] = twr_frame_seq++;
		resp_template[5] = msg->src_addr16 & 0xFF;
		resp_template[6] = (msg->src_addr16 >> 8) & 0xFF;
		for (int i = 0; i < 4; i++) {
			resp_template[10 + i] = (uint8_t)((uint32_t)(poll_rx_ts >> (i * 8)) & 0xFF);
			resp_template[14 + i] = (uint8_t)((resp_tx_ts_est >> (i * 8)) & 0xFF);
		}

		dwt_setdelayedtrxtime(resp_tx_time);
		dwt_writetxdata(20, resp_template, 0);
		dwt_writetxfctrl(20, 0, 1);
		if (dwt_starttx(DWT_START_TX_DELAYED) != DWT_SUCCESS)
			return 0;

		{
			unsigned long deadline = portGetTickCount() + MAIN_TX_TIMEOUT_MS;
			while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS)) {
				if ((signed long)(portGetTickCount() - deadline) > 0) {
					dwt_forcetrxoff();
					dwt_rxenable(DWT_START_RX_IMMEDIATE);
					return 0;
				}
			}
		}
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
		return 1;
	}
	return 0;
}

/* Called from net_rx_ok_isr for time-critical TWR POLL frames.
 * Uses pre-built resp_template — only seq_num, dest_addr and two
 * timestamps are updated per call, keeping ISR as short as possible.
 * Busy-waits for TX completion — acceptable because the radio is
 * half-duplex and no frames can arrive during that window. */
void ss_twr_isr(const uint8_t *frame, uint16_t len)
{
	uint64_t poll_rx_ts = 0;
	uint8_t ts_tab[5];
	uint32 resp_tx_time;
	uint32_t resp_tx_ts_est;

	(void)len;

	dwt_readrxtimestamp(ts_tab);
	for (int i = 4; i >= 0; i--) {
		poll_rx_ts <<= 8;
		poll_rx_ts |= ts_tab[i];
	}

	net_addr16_t src_addr = frame[7] | ((net_addr16_t)frame[8] << 8);

	resp_tx_time = (uint32)((poll_rx_ts +
		(uint64_t)POLL_RX_TO_RESP_TX_DLY_UUS * UUS_TO_DWT_TIME) >> 8);
	resp_tx_time &= 0xFFFFFFFEUL;
	resp_tx_ts_est = (resp_tx_time << 8) + TX_ANT_DLY;

	resp_template[2] = twr_frame_seq++;
	resp_template[5] = src_addr & 0xFF;
	resp_template[6] = (src_addr >> 8) & 0xFF;
	for (int i = 0; i < 4; i++) {
		resp_template[10 + i] = (uint8_t)((uint32_t)(poll_rx_ts >> (i * 8)) & 0xFF);
		resp_template[14 + i] = (uint8_t)((resp_tx_ts_est >> (i * 8)) & 0xFF);
	}

	dwt_setdelayedtrxtime(resp_tx_time);
	dwt_writetxdata(20, resp_template, 0);
	dwt_writetxfctrl(20, 0, 1);
	if (dwt_starttx(DWT_START_TX_DELAYED) == DWT_SUCCESS) {
		uint32_t iters = ISR_TX_TIMEOUT_ITERS;
		while (!(dwt_read32bitreg(SYS_STATUS_ID) & SYS_STATUS_TXFRS)) {
			if (--iters == 0) {
				dwt_forcetrxoff();
				break;
			}
		}
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
	}
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

float ss_twr_read_temperature(void)
{
	uint8 raw = (dwt_readtempvbat(1) & 0xFF00u) >> 8;
	g_last_temp = ((float)raw - (float)dwt_geticreftemp()) * 1.14f + 23.0f;
	return g_last_temp;
}

void ss_twr_set_ant_dly(uint16_t tx_dly, uint16_t rx_dly)
{
	g_tx_ant_dly = tx_dly;
	g_rx_ant_dly = rx_dly;
	dwt_settxantennadelay(tx_dly);
	dwt_setrxantennadelay(rx_dly);
}

void ss_twr_set_temp_coef(float k)
{
	g_k_temp = k;
}
