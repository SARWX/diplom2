#include "port.h"
#include "deca_device_api.h"
#include "device_id.h"
#include "net_mac.h"
#include "main_anchor.h"
#include "anchor.h"
#include "tag.h"

/*==============================================================================
 * Hardware Configuration
 *============================================================================*/

static const dwt_config_t config = {
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

static const device_registration_t devices[] = {
    {0x12345678, &DEVICE_MAIN_ANCHOR, main_anchor_init, main_anchor_loop},
    {0x87654321, &DEVICE_ANCHOR,      anchor_init,      anchor_loop},
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
    device_config_t* my_device = device_init_from_hardware();
    if (!my_device) {
        while (1);
    }
    
    /* Initialize network */
    int use_eui64 = (my_device->type == DEVICE_TYPE_TAG) ? 1 : 0;
    net_addr16_t short_addr = use_eui64 ? 0 : my_device->short_addr;
    const net_eui64_t* eui64 = use_eui64 ? &my_device->eui64 : NULL;
    
    if (net_init(use_eui64, short_addr, eui64, NET_FILTER_DATA) != 0) {
        while (1);
    }
    
    /* Run device */
    if (my_device->init_func) {
        my_device->init_func(my_device);
    }
    
    while (1) {
        if (my_device->main_loop_func) {
            my_device->main_loop_func(my_device);
        }
    }
    
    return 0;
}
