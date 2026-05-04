#include "port.h"
#include "deca_device_api.h"
#include "device_id.h"
#include "net_mac.h"
#include "main_anchor.h"
#include "anchor.h"
#include "tag.h"
#include "common.h"

/*==============================================================================
 * Hardware Configuration
 *============================================================================*/

/** @brief DW1000 radio configuration: channel 2, 64 MHz PRF, 6.8 Mbps, 128-chip preamble. */
static dwt_config_t config = {
	2,               /* Channel number */
	DWT_PRF_64M,     /* Pulse repetition frequency */
	DWT_PLEN_128,    /* Preamble length */
	DWT_PAC8,        /* Preamble acquisition chunk size */
	9,               /* TX preamble code */
	9,               /* RX preamble code */
	0,               /* Use standard SFD */
	DWT_BR_6M8,      /* Data rate */
	DWT_PHRMODE_STD, /* PHY header mode */
	(129 + 8 - 8)    /* SFD timeout */
};

/*==============================================================================
 * Device Registration
 *============================================================================*/

/** @brief Static table mapping DW1000 Part IDs to device configs and entry points. */
static const device_registration_t devices[] = {
	{0x545541A2, {{0x01,0,0,0,0,0,0,0}}, &DEVICE_MAIN_ANCHOR, main_anchor_init, main_anchor_loop},
	{0x1454DA34, {{0x02,0,0,0,0,0,0,0}}, &DEVICE_ANCHOR,      anchor_init,      anchor_loop},
	{0x4DA298B5, {{0x03,0,0,0,0,0,0,0}}, &DEVICE_ANCHOR,      anchor_init,      anchor_loop},
	{0x0DA2C13B, {{0x04,0,0,0,0,0,0,0}}, &DEVICE_ANCHOR,      anchor_init,      anchor_loop},
	{0x157240B1, {{0x05,0,0,0,0,0,0,0}}, &DEVICE_TAG,         tag_init,         tag_loop},
};

#define NUM_DEVICES (sizeof(devices) / sizeof(devices[0]))

/*==============================================================================
 * Main
 *============================================================================*/

int main(void)
{
	/* Hardware initialization */
	peripherals_init();
	reset_DW1000();
	
	spi_set_rate_low();
	if (dwt_initialise(DWT_LOADUCODE | DWT_READ_OTP_LID | DWT_READ_OTP_PID) == DWT_ERROR) {
		while (1);
	}
	spi_set_rate_high();

	dwt_configure(&config);
	dwt_setleds(DWT_LEDS_ENABLE);

	for (uint8_t i = 0; i < NUM_DEVICES; i++) {
		device_register(&devices[i]);
	}

	if (!device_init_from_hardware()) {
		while (1);
	}
	common_srand(curr_dev->part_id);

	if (net_init(0, &curr_dev->eui64, DWT_FF_DATA_EN))
		while (1);

	if (curr_dev->init_func)
		curr_dev->init_func();

	while (1) {
		if (curr_dev->main_loop_func)
			curr_dev->main_loop_func();
	}

	return 0;
}
