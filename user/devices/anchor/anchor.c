#include "anchor.h"
#include "net_mac.h"
#include "cmd_parser.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include "port.h"
#include "sleep.h"
#include <string.h>
#include <stdlib.h>

#define RESPONSE_PAYLOAD ((const uint8_t*)"A")
#define RESPONSE_PAYLOAD_LEN (sizeof("A") - 1)

/*==============================================================================
 * Command Processing for Anchor
 *============================================================================*/

static int process_anchor_command(cmd_parse_result_t *result, net_message_t *msg)
{
    if (!result->valid)
        return -1;

    switch (result->code) {
        case CMD_DISCOVER:
            sleep_ms((uint32_t)rand() % 1000);
            /* Response UNICAST to sender (main station) */
            if (msg->src_is_eui64) {
                net_send_to_64bit(&msg->src_eui64, RESPONSE_PAYLOAD,
                                                RESPONSE_PAYLOAD_LEN);
            } else {
                net_send_to_16bit(msg->src_addr16, RESPONSE_PAYLOAD,
                                                RESPONSE_PAYLOAD_LEN);
            }
            break;
            
        case CMD_CONFIG_START:
            net_state.mode = NET_MODE_CONFIG;
            break;

        case CMD_CONFIG_STOP:
            net_state.mode = NET_MODE_IDLE;
            break;
            
        case CMD_RANGING_START:
            net_state.mode =NET_MODE_RANGING;
            break;
            
        case CMD_RANGING_STOP:
            net_state.mode = NET_MODE_IDLE;
            break;
            
        default:
            /* Unknown command - ignore */
            break;
    }

    return 0;
}

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
    
    /* Parse payload as command string (для любых сообщений, не только broadcast) */
    if (msg.payload_len > 0 && msg.payload_len <= MAX_PAYLOAD_SIZE) {
        char cmd_buffer[MAX_PAYLOAD_SIZE + 1];
        memcpy(cmd_buffer, msg.payload, msg.payload_len);
        cmd_buffer[msg.payload_len] = '\0';
        
        cmd_parse_result_t result = cmd_parse(cmd_buffer);
        process_anchor_command(&result, &msg);
    }
    
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
}

void anchor_loop(void)
{
    ;
}
