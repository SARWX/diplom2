#include "anchor.h"
#include "net_mac.h"
#include "deca_device_api.h"
#include "port.h"
#include <string.h>

#define DISCOVER_PAYLOAD "DISCOVER"
#define RESPONSE_PAYLOAD "A"

static uint8_t initialized = 0;

static void rx_ok_cb(const dwt_cb_data_t *cb_data)
{
    net_message_t msg;
    
    if (cb_data->datalength > sizeof(net_state.rx_buffer))
        return;
    
    dwt_readrxdata(net_state.rx_buffer, cb_data->datalength, 0);
    
    if (!net_parse_message(net_state.rx_buffer, cb_data->datalength, &msg))
        return;
    
    /* Проверяем broadcast и DISCOVER */
    if (net_is_broadcast(&msg) && 
        msg.payload_len == sizeof(DISCOVER_PAYLOAD) - 1 && /* Сомнительная вещь */
        memcmp(msg.payload, DISCOVER_PAYLOAD, msg.payload_len) == 0)
    {
        net_send_broadcast((const uint8_t*)RESPONSE_PAYLOAD, sizeof(RESPONSE_PAYLOAD) - 1);
    }
    
    /* Снова включаем приём */
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

static void rx_err_cb(const dwt_cb_data_t *cb_data)
{
    (void)cb_data;
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
}

void anchor_init(device_config_t* dev)
{
    (void)dev;
    
    port_set_deca_isr(dwt_isr);
    dwt_setcallbacks(NULL, rx_ok_cb, NULL, rx_err_cb);
    dwt_setinterrupt(DWT_INT_RFCG | DWT_INT_RPHE | DWT_INT_RFCE | DWT_INT_RFSL, 1);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    
    initialized = 1;
}

void anchor_loop(device_config_t* dev)
{
    (void)dev;
    /* Всё делается в прерываниях */
}
