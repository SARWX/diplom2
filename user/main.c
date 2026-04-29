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

/** @brief DW1000 radio configuration: channel 2, 64 MHz PRF, 110 kbps, 1024-chip preamble. */
static dwt_config_t config = {
	2,               /* Channel number */
	DWT_PRF_64M,     /* Pulse repetition frequency */
	DWT_PLEN_1024,   /* Preamble length */
	DWT_PAC32,       /* Preamble acquisition chunk size */
	9,               /* TX preamble code */
	9,               /* RX preamble code */
	1,               /* Use non-standard SFD */
	DWT_BR_110K,     /* Data rate */
	DWT_PHRMODE_STD, /* PHY header mode */
	(1025 + 64 - 32) /* SFD timeout */
};

/*==============================================================================
 * Device Registration
 *============================================================================*/

/** @brief Static table mapping DW1000 Part IDs to device configs and entry points. */
static const device_registration_t devices[] = {
	{0x545541A2, &DEVICE_MAIN_ANCHOR, main_anchor_init, main_anchor_loop},
	{0x1454DA34, &DEVICE_ANCHOR,      anchor_init,      anchor_loop},
	{0xABCD1234, &DEVICE_TAG,         tag_init,         tag_loop},
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
	if (dwt_initialise(DWT_READ_OTP_LID | DWT_READ_OTP_PID) == DWT_ERROR) {
		while (1);
	}
	spi_set_rate_high();
	
	dwt_configure(&config);
	dwt_setleds(DWT_LEDS_ENABLE);
	
	/* Register all devices */
	for (uint8_t i = 0; i < NUM_DEVICES; i++) {
		device_register(&devices[i]);
	}
	
	/* Detect current device */
	if (!device_init_from_hardware()) {
		while (1);
	}
	/* Setup rand generators for different start values */
	common_srand(curr_dev->part_id);
	
	/* Initialize network */
	if (net_init(0, &curr_dev->eui64, DWT_FF_DATA_EN))
		while (1);

	/* Run device */
	if (curr_dev->init_func)
		curr_dev->init_func();
	
	while (1) {
		if (curr_dev->main_loop_func)
			curr_dev->main_loop_func();
	}
	
	return 0;
}
