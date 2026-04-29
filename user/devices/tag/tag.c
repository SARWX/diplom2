#include "tag.h"
#include "net_dispatch.h"
#include "net_devices.h"

/** @brief List of network devices received during enumeration. */
static net_devices_list_t devices;

void tag_init(void)
{
	net_radio_init();
	net_devices_init(&devices);
}

void tag_loop(void)
{
	net_process(&devices, NULL);
}
