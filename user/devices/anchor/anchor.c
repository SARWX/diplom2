#include "anchor.h"
#include "ss_twr.h"
#include "deca_device_api.h"
#include "deca_regs.h"

#define TX_ANT_DLY 16724
#define RX_ANT_DLY 16724

void anchor_init(void)
{
	dwt_setrxantennadelay(RX_ANT_DLY);
	dwt_settxantennadelay(TX_ANT_DLY);
	dwt_enableframefilter(DWT_FF_NOTYPE_EN);
}

void anchor_loop(void)
{
	uint32_t status_reg;
	uint8_t rx_buf[12];

	dwt_rxenable(DWT_START_RX_IMMEDIATE);

	while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) &
		 (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_ERR)))
		;

	if (status_reg & SYS_STATUS_RXFCG) {
		uint32_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
		if (frame_len <= sizeof(rx_buf))
			dwt_readrxdata(rx_buf, frame_len, 0);
		ss_twr_handle_poll(rx_buf, frame_len);
	} else {
		dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
		dwt_rxreset();
	}
}
