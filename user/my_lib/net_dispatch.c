#include "net_dispatch.h"
#include "net_mac.h"
#include "enumeration.h"
#include "configuration.h"
#include "deca_device_api.h"
#include "port.h"

void net_radio_init(void)
{
	/* Antenna delays for accurate timestamping */
	dwt_setrxantennadelay(16436);
	dwt_settxantennadelay(16436);

	port_set_deca_isr(dwt_isr);
	dwt_setcallbacks(NULL, net_rx_ok_isr, net_rx_to_isr, net_rx_err_isr);
	dwt_setinterrupt(DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE |
			 DWT_INT_RFSL | DWT_INT_RFTO | DWT_INT_RXPTO | DWT_INT_SFDT, 1);
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

int net_process(net_devices_list_t *devices, net_idle_fn_t idle_fn)
{
	net_message_t msg;
	if (!net_rx_poll(&msg))
		return 0;

	switch (net_state.mode) {
	case NET_MODE_ENUMERATION:
	case NET_MODE_SYNC_WAIT:
		enumeration_handle_message(devices, &msg);
		break;
	case NET_MODE_CONFIG:
		configuration_handle_message(devices, &msg);
		break;
	default:
		if (idle_fn)
			idle_fn(devices, &msg);
		break;
	}

rearm:
	dwt_rxenable(DWT_START_RX_IMMEDIATE);
	return 1;
}
