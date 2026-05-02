#include "anchor.h"
#include "net_dispatch.h"
#include "net_devices.h"
#include "net_mac.h"
#include "deca_device_api.h"

#define TX_ANT_DLY 16724
#define RX_ANT_DLY 16724

static net_devices_list_t devices;

void anchor_init(void)
{
	net_radio_init();
	dwt_setrxantennadelay(RX_ANT_DLY);
	dwt_settxantennadelay(TX_ANT_DLY);
	/* Hardcoded short address 2 for testing */
	net_state.short_addr = 2;
	dwt_setaddress16(2);
	net_devices_init(&devices);
}

void anchor_loop(void)
{
	net_process(&devices, NULL);
}
