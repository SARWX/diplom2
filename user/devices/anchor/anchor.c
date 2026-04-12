#include "anchor.h"
#include "net_mac.h"
#include "net_devices.h"
#include "cmd_parser.h"
#include "enumeration.h"
#include "deca_device_api.h"
#include "port.h"
#include <string.h>
#include <stdlib.h>

static net_devices_list_t devices;

/*==============================================================================
 * RX Callback
 *============================================================================*/

static void rx_ok_cb(const dwt_cb_data_t *cb_data)
{
    net_message_t msg;
    
    if (cb_data->datalength > sizeof(net_state.rx_buffer))
        return;
    
    dwt_readrxdata(net_state.rx_buffer, cb_data->datalength, 0);
    
    if (!net_parse_message(net_state.rx_buffer, cb_data->datalength, &msg))
        return;
    
    /* Передаём сообщение модулю энумерации */
    enumeration_handle_message(&devices, &msg);
    
    /* Re-enable reception */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void rx_to_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    if (net_state.mode == NET_MODE_IDLE)
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void rx_err_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

/*==============================================================================
 * Public Functions
 *============================================================================*/

void anchor_init(void)
{
    port_set_deca_isr(dwt_isr);
    dwt_setcallbacks(NULL, rx_ok_cb, rx_to_cb, rx_err_cb);
    dwt_setinterrupt(DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE |
                        DWT_INT_RFSL | DWT_INT_RFTO , 1);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    
    net_devices_init(&devices);
}

void anchor_loop(void)
{
    /* Всё в прерываниях */
}
