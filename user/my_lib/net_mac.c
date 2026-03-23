#include "net_mac.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include <string.h>
#include <stdio.h>

/*==============================================================================
 * Internal State
 *============================================================================*/

static struct {
    uint8_t initialized;
    net_addr16_t short_addr;
    net_eui64_t eui64;
    net_rx_callback_t rx_callback;
    uint8_t rx_buffer[128];
} net_state = {0};

/*==============================================================================
 * Internal Helper Functions
 *============================================================================*/

/* Build MAC header */
static void build_mac_header(uint8_t* buffer, uint16_t frame_ctrl, uint8_t seq_num,
                              uint16_t pan_id, uint16_t dest_addr, uint16_t src_addr)
{
    buffer[0] = frame_ctrl & 0xFF;
    buffer[1] = (frame_ctrl >> 8) & 0xFF;
    buffer[2] = seq_num;
    buffer[3] = pan_id & 0xFF;
    buffer[4] = (pan_id >> 8) & 0xFF;
    buffer[5] = dest_addr & 0xFF;
    buffer[6] = (dest_addr >> 8) & 0xFF;
    buffer[7] = src_addr & 0xFF;
    buffer[8] = (src_addr >> 8) & 0xFF;
}
// Это не для этого файла
// /* Initialize DW1000 hardware */
// static int hw_init(void)
// {
//     reset_DW1000();
    
//     spi_set_rate_low();
//     if (dwt_initialise(DWT_LOADNONE) == DWT_ERROR) {
//         return -1;
//     }
//     spi_set_rate_high();
    
//     /* Default configuration */
//     dwt_config_t config = {
//         2, DWT_PRF_64M, DWT_PLEN_1024, DWT_PAC32,
//         9, 9, 1, DWT_BR_110K, DWT_PHRMODE_STD,
//         (1025 + 64 - 32)
//     };
    
//     dwt_configure(&config);
//     dwt_setleds(DWT_LEDS_ENABLE);
    
//     return 0;
// }

/*==============================================================================
 * Public Functions - Initialization
 *============================================================================*/

int net_init(net_addr16_t short_addr, const net_eui64_t* eui64, uint16_t filter_mask)
{
    /* Initialize hardware */
    if (hw_init() != 0) {
        return -1;
    }
    
    /* Store addresses */
    net_state.short_addr = short_addr;
    if (eui64) {
        memcpy(&net_state.eui64, eui64, sizeof(net_eui64_t));
    } else {
        memset(&net_state.eui64, 0, sizeof(net_eui64_t));
    }
    
    /* Set PAN ID and addresses for frame filtering */
    dwt_setpanid(NET_PAN_ID);
    dwt_setaddress16(short_addr);
    if (eui64) {
        dwt_seteui((uint8_t*)eui64);
    }
    
    /* Enable frame filtering */
    if (filter_mask != NET_FILTER_NONE) {
        dwt_enableframefilter(filter_mask);
    }
    
    net_state.initialized = 1;
    
    return 0;
}

void net_set_rx_callback(net_rx_callback_t callback)
{
    net_state.rx_callback = callback;
}

net_addr16_t net_get_short_addr(void)
{
    return net_state.short_addr;
}

/*==============================================================================
 * Public Functions - Message Building
 *============================================================================*/

uint16_t net_build_broadcast(uint8_t* buffer, net_addr16_t src_addr, uint8_t seq_num,
                              const uint8_t* payload, uint16_t payload_len)
{
    const uint16_t header_len = 9;
    
    build_mac_header(buffer, NET_FRAME_CTRL_16BIT, seq_num, NET_PAN_ID,
                     NET_BROADCAST_ADDR, src_addr);
    
    if (payload && payload_len > 0) {
        memcpy(buffer + header_len, payload, payload_len);
        return header_len + payload_len;
    }
    
    return header_len;
}

uint16_t net_build_unicast(uint8_t* buffer, net_addr16_t dst_addr, net_addr16_t src_addr,
                            uint8_t seq_num, const uint8_t* payload, uint16_t payload_len)
{
    const uint16_t header_len = 9;
    
    build_mac_header(buffer, NET_FRAME_CTRL_16BIT, seq_num, NET_PAN_ID,
                     dst_addr, src_addr);
    
    if (payload && payload_len > 0) {
        memcpy(buffer + header_len, payload, payload_len);
        return header_len + payload_len;
    }
    
    return header_len;
}

uint16_t net_build_response(uint8_t* buffer, const net_eui64_t* dst_eui,
                             net_addr16_t src_addr, uint8_t seq_num,
                             const uint8_t* payload, uint16_t payload_len)
{
    const uint16_t header_len = 13; /* 2+1+2+8 = 13 for 64-bit dest */
    
    /* Frame control (0x8C41 - data frame with 16-bit src, 64-bit dest) */
    buffer[0] = 0x41;
    buffer[1] = 0x8C;
    buffer[2] = seq_num;
    buffer[3] = 0x9A;
    buffer[4] = 0x60;  /* Application ID */
    
    /* Destination address (64-bit) */
    memcpy(buffer + 5, dst_eui, 8);
    
    /* Source address (16-bit) */
    buffer[13] = src_addr & 0xFF;
    buffer[14] = (src_addr >> 8) & 0xFF;
    
    /* Payload */
    if (payload && payload_len > 0) {
        memcpy(buffer + 15, payload, payload_len);
        return header_len + payload_len;
    }
    
    return header_len;
}

/*==============================================================================
 * Public Functions - Transmission
 *============================================================================*/

int net_send_frame(uint8_t* frame, uint16_t frame_len, uint8_t response_expected)
{
    uint32_t status_reg;
    
    if (!net_state.initialized) {
        return -1;
    }
    
    /* Write frame to DW1000 */
    dwt_writetxdata(frame_len, frame, 0);
    dwt_writetxfctrl(frame_len, 0, 0);
    
    /* Start transmission */
    if (response_expected) {
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
    } else {
        dwt_starttx(DWT_START_TX_IMMEDIATE);
    }
    
    /* Wait for transmission to complete */
    while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & SYS_STATUS_TXFRS)) {
        if (status_reg & SYS_STATUS_TXERR) {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXERR);
            return -1;
        }
    }
    
    /* Clear TX frame sent event */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
    
    /* Wait for response if expected */
    if (response_expected) {
        /* Poll for response or timeout */
        while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & 
                 (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR))) {
            /* Wait */
        }
        
        if (status_reg & SYS_STATUS_RXFCG) {
            uint16_t rx_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
            if (rx_len <= sizeof(net_state.rx_buffer)) {
                dwt_readrxdata(net_state.rx_buffer, rx_len, 0);
            }
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
            return 1;
        } else {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_TO | SYS_STATUS_ALL_RX_ERR);
            return 0;
        }
    }
    
    return 1;
}

int net_send_broadcast(net_addr16_t src_addr, const uint8_t* payload, uint16_t payload_len)
{
    uint8_t frame[128];
    static uint8_t seq_num = 0;
    uint16_t frame_len;
    
    frame_len = net_build_broadcast(frame, src_addr, seq_num++, payload, payload_len);
    return net_send_frame(frame, frame_len, 0);
}

int net_send_broadcast_with_response(net_addr16_t src_addr, const uint8_t* payload, uint16_t payload_len)
{
    uint8_t frame[128];
    static uint8_t seq_num = 0;
    uint16_t frame_len;
    
    frame_len = net_build_broadcast(frame, src_addr, seq_num++, payload, payload_len);
    return net_send_frame(frame, frame_len, 1);
}

int net_send_unicast(net_addr16_t dst_addr, net_addr16_t src_addr,
                      const uint8_t* payload, uint16_t payload_len)
{
    uint8_t frame[128];
    static uint8_t seq_num = 0;
    uint16_t frame_len;
    
    frame_len = net_build_unicast(frame, dst_addr, src_addr, seq_num++, payload, payload_len);
    return net_send_frame(frame, frame_len, 0);
}

/*==============================================================================
 * Public Functions - Reception
 *============================================================================*/

int net_parse_message(uint8_t* buffer, uint16_t len, net_message_t* msg)
{
    if (!buffer || !msg || len < 9) {
        return 0;
    }
    
    msg->data = buffer;
    msg->len = len;
    msg->seq_num = buffer[2];
    msg->dst_addr = buffer[5] | (buffer[6] << 8);
    msg->src_addr = buffer[7] | (buffer[8] << 8);
    msg->payload = buffer + 9;
    msg->payload_len = len - 9;
    
    return 1;
}

int net_is_broadcast(const net_message_t* msg)
{
    return msg && (msg->dst_addr == NET_BROADCAST_ADDR);
}

int net_receive_once(void)
{
    uint32_t status_reg;
    net_message_t msg;
    
    if (!net_state.initialized) {
        return -1;
    }
    
    /* Check if frame already received */
    status_reg = dwt_read32bitreg(SYS_STATUS_ID);
    
    if (status_reg & SYS_STATUS_RXFCG) {
        uint16_t frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
        
        if (frame_len <= sizeof(net_state.rx_buffer)) {
            dwt_readrxdata(net_state.rx_buffer, frame_len, 0);
            
            if (net_parse_message(net_state.rx_buffer, frame_len, &msg)) {
                if (net_state.rx_callback) {
                    net_state.rx_callback(&msg);
                }
            }
        }
        
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
        return 1;
    }
    
    /* Clear errors if any */
    if (status_reg & SYS_STATUS_ALL_RX_ERR) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
    }
    
    return 0;
}

int net_receive_blocking(uint32_t timeout_ms)
{
    uint32_t start_time = 0; /* Would use actual timer in real implementation */
    uint32_t status_reg;
    
    /* Enable receiver */
    dwt_setrxtimeout(0);
    dwt_rxenable(DWT_START_RX_IMMEDIATE);
    
    while (1) {
        status_reg = dwt_read32bitreg(SYS_STATUS_ID);
        
        if (status_reg & SYS_STATUS_RXFCG) {
            return net_receive_once();
        }
        
        if (status_reg & SYS_STATUS_ALL_RX_ERR) {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
            dwt_rxenable(DWT_START_RX_IMMEDIATE);
        }
        
        /* Check timeout (simplified) */
        if (timeout_ms > 0) {
            /* Would need actual timer implementation */
        }
    }
    
    return 0;
}

/*==============================================================================
 * Public Functions - Utilities
 *============================================================================*/

void net_addr16_to_str(net_addr16_t addr, char* str)
{
    if (str) {
        sprintf(str, "0x%04X", addr);
    }
}

void net_eui64_to_str(const net_eui64_t* eui, char* str)
{
    if (str && eui) {
        sprintf(str, "%02X:%02X:%02X:%02X:%02X:%02X:%02X:%02X",
                eui->bytes[0], eui->bytes[1], eui->bytes[2], eui->bytes[3],
                eui->bytes[4], eui->bytes[5], eui->bytes[6], eui->bytes[7]);
    }
}

net_addr16_t net_str_to_addr16(const char* str)
{
    unsigned int addr;
    if (sscanf(str, "%x", &addr) == 1) {
        return (net_addr16_t)addr;
    }
    return 0;
}
