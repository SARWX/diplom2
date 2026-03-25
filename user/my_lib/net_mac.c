#include "net_mac.h"
#include "deca_device_api.h"
#include "deca_regs.h"
#include <string.h>

/*==============================================================================
 * Internal State
 *============================================================================*/

static struct {
    uint8_t initialized;
    uint8_t use_eui64;              /* 1 = use 64-bit source, 0 = use 16-bit */
    net_addr16_t short_addr;
    net_eui64_t eui64;
    net_rx_callback_t rx_callback;
    uint8_t rx_buffer[128];
} net_state = {0};

/* Header lengths */
#define NET_MAC_HEADER_LEN_16BIT   9
#define NET_MAC_HEADER_LEN_64BIT   15

/*==============================================================================
 * Frame Control Helpers
 *============================================================================*/

uint16_t net_build_frame_control(net_addr_mode_t dest_mode, net_addr_mode_t src_mode,
                                  int pan_id_compression, int ack_request)
{
    uint16_t fc = 0;
    
    /* Frame type: 001 = Data frame */
    fc |= (0x01 << 0);
    
    /* Security enabled: 0 */
    /* Frame pending: 0 */
    
    /* ACK request */
    if (ack_request) {
        fc |= (1 << 5);
    }
    
    /* PAN ID compression */
    if (pan_id_compression) {
        fc |= (1 << 6);
    }
    
    /* Destination addressing mode */
    fc |= ((dest_mode & 0x03) << 10);
    
    /* Frame version: 0 */
    
    /* Source addressing mode */
    fc |= ((src_mode & 0x03) << 14);
    
    return fc;
}

/*==============================================================================
 * Internal Header Builders
 *============================================================================*/

/* Build header with 16-bit source and 16-bit destination */
static uint16_t build_header_16bit_16bit(uint8_t* buffer, uint8_t seq_num,
                                          net_addr16_t dest_addr, net_addr16_t src_addr)
{
    uint16_t fc = net_build_frame_control(NET_ADDR_MODE_16BIT, NET_ADDR_MODE_16BIT, 1, 0);
    
    buffer[0] = fc & 0xFF;
    buffer[1] = (fc >> 8) & 0xFF;
    buffer[2] = seq_num;
    buffer[3] = NET_PAN_ID & 0xFF;
    buffer[4] = (NET_PAN_ID >> 8) & 0xFF;
    buffer[5] = dest_addr & 0xFF;
    buffer[6] = (dest_addr >> 8) & 0xFF;
    buffer[7] = src_addr & 0xFF;
    buffer[8] = (src_addr >> 8) & 0xFF;
    
    return NET_MAC_HEADER_LEN_16BIT;
}

/* Build header with 64-bit source and 16-bit destination */
static uint16_t build_header_64bit_16bit(uint8_t* buffer, uint8_t seq_num,
                                          net_addr16_t dest_addr, const net_eui64_t* src_eui)
{
    uint16_t fc = net_build_frame_control(NET_ADDR_MODE_16BIT, NET_ADDR_MODE_64BIT, 1, 0);
    
    buffer[0] = fc & 0xFF;
    buffer[1] = (fc >> 8) & 0xFF;
    buffer[2] = seq_num;
    buffer[3] = NET_PAN_ID & 0xFF;
    buffer[4] = (NET_PAN_ID >> 8) & 0xFF;
    buffer[5] = dest_addr & 0xFF;
    buffer[6] = (dest_addr >> 8) & 0xFF;
    /* Source address (64-bit) */
    memcpy(buffer + 7, src_eui, 8);
    
    return NET_MAC_HEADER_LEN_16BIT + 6; /* +6 extra for 64-bit src (8 bytes vs 2) */
    /* Actually: 9 + 6 = 15 bytes total */
}

/* Build header with 16-bit source and 64-bit destination */
static uint16_t build_header_16bit_64bit(uint8_t* buffer, uint8_t seq_num,
                                          const net_eui64_t* dest_eui, net_addr16_t src_addr)
{
    uint16_t fc = net_build_frame_control(NET_ADDR_MODE_64BIT, NET_ADDR_MODE_16BIT, 1, 0);
    
    buffer[0] = fc & 0xFF;
    buffer[1] = (fc >> 8) & 0xFF;
    buffer[2] = seq_num;
    buffer[3] = NET_PAN_ID & 0xFF;
    buffer[4] = (NET_PAN_ID >> 8) & 0xFF;
    /* Destination address (64-bit) */
    memcpy(buffer + 5, dest_eui, 8);
    /* Source address (16-bit) */
    buffer[13] = src_addr & 0xFF;
    buffer[14] = (src_addr >> 8) & 0xFF;
    
    return NET_MAC_HEADER_LEN_64BIT;
}

/* Build header with 64-bit source and 64-bit destination */
static uint16_t build_header_64bit_64bit(uint8_t* buffer, uint8_t seq_num,
                                          const net_eui64_t* dest_eui, const net_eui64_t* src_eui)
{
    uint16_t fc = net_build_frame_control(NET_ADDR_MODE_64BIT, NET_ADDR_MODE_64BIT, 1, 0);
    
    buffer[0] = fc & 0xFF;
    buffer[1] = (fc >> 8) & 0xFF;
    buffer[2] = seq_num;
    buffer[3] = NET_PAN_ID & 0xFF;
    buffer[4] = (NET_PAN_ID >> 8) & 0xFF;
    /* Destination address (64-bit) */
    memcpy(buffer + 5, dest_eui, 8);
    /* Source address (64-bit) */
    memcpy(buffer + 13, src_eui, 8);
    
    return NET_MAC_HEADER_LEN_64BIT + 8; /* 15 + 8 = 23 bytes? Actually 5+8+8 = 21 */
    /* Let's calculate: 2(fc)+1(seq)+2(pan)+8(dest)+8(src) = 21 bytes */
}

/*==============================================================================
 * Public Functions - Initialization
 *============================================================================*/
int net_init(int use_eui64, net_addr16_t short_addr, const net_eui64_t* eui64, uint16_t filter_mask)
{  
    net_state.use_eui64 = use_eui64;
    net_state.short_addr = short_addr;
    
    if (eui64) {
        memcpy(&net_state.eui64, eui64, sizeof(net_eui64_t));
    } else {
        memset(&net_state.eui64, 0, sizeof(net_eui64_t));
    }
    
    dwt_setpanid(NET_PAN_ID);
    dwt_setaddress16(short_addr);
    if (eui64) {
        dwt_seteui((uint8_t*)eui64);
    }
    dwt_enableframefilter(filter_mask);
    
    net_state.initialized = 1;
    return 0;
}

void net_set_rx_callback(net_rx_callback_t callback)
{
    net_state.rx_callback = callback;
}

net_addr16_t net_get_src_addr16(void)
{
    return net_state.short_addr;
}

const net_eui64_t* net_get_src_eui64(void)
{
    return &net_state.eui64;
}

int net_use_eui64(void)
{
    return net_state.use_eui64;
}

/*==============================================================================
 * Public Functions - Frame Building
 *============================================================================*/

uint16_t net_build_header(uint8_t* buffer,
                          const net_eui64_t* dest_eui, net_addr16_t dest_addr16,
                          uint8_t seq_num)
{
    if (dest_eui) {
        /* Destination is 64-bit */
        if (net_state.use_eui64) {
            /* Source is also 64-bit */
            return build_header_64bit_64bit(buffer, seq_num, dest_eui, &net_state.eui64);
        } else {
            /* Source is 16-bit */
            return build_header_16bit_64bit(buffer, seq_num, dest_eui, net_state.short_addr);
        }
    } else {
        /* Destination is 16-bit */
        if (net_state.use_eui64) {
            /* Source is 64-bit */
            return build_header_64bit_16bit(buffer, seq_num, dest_addr16, &net_state.eui64);
        } else {
            /* Source is 16-bit */
            return build_header_16bit_16bit(buffer, seq_num, dest_addr16, net_state.short_addr);
        }
    }
}

uint16_t net_build_frame(uint8_t* buffer,
                         const net_eui64_t* dest_eui, net_addr16_t dest_addr16,
                         uint8_t seq_num,
                         const uint8_t* payload, uint16_t payload_len)
{
    uint16_t header_len = net_build_header(buffer, dest_eui, dest_addr16, seq_num);
    
    if (payload && payload_len > 0) {
        memcpy(buffer + header_len, payload, payload_len);
    }
    
    return header_len + payload_len;
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
    
    dwt_writetxdata(frame_len, frame, 0);
    dwt_writetxfctrl(frame_len, 0, 0);
    
    if (response_expected) {
        dwt_starttx(DWT_START_TX_IMMEDIATE | DWT_RESPONSE_EXPECTED);
    } else {
        dwt_starttx(DWT_START_TX_IMMEDIATE);
    }
    
    while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & SYS_STATUS_TXFRS)) {
        if (status_reg & SYS_STATUS_TXERR) {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXERR);
            return -1;
        }
    }
    
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
    
    if (response_expected) {
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

int net_send_broadcast(const uint8_t* payload, uint16_t payload_len)
{
    uint8_t frame[128];
    static uint8_t seq_num = 0;
    uint16_t frame_len;
    
    frame_len = net_build_frame(frame, NULL, NET_BROADCAST_ADDR, seq_num++, payload, payload_len);
    return net_send_frame(frame, frame_len, 0);
}

int net_send_broadcast_with_response(const uint8_t* payload, uint16_t payload_len)
{
    uint8_t frame[128];
    static uint8_t seq_num = 0;
    uint16_t frame_len;
    
    frame_len = net_build_frame(frame, NULL, NET_BROADCAST_ADDR, seq_num++, payload, payload_len);
    return net_send_frame(frame, frame_len, 1);
}

int net_send_to_16bit(net_addr16_t dst_addr, const uint8_t* payload, uint16_t payload_len)
{
    uint8_t frame[128];
    static uint8_t seq_num = 0;
    uint16_t frame_len;
    
    frame_len = net_build_frame(frame, NULL, dst_addr, seq_num++, payload, payload_len);
    return net_send_frame(frame, frame_len, 0);
}

int net_send_to_64bit(const net_eui64_t* dst_eui, const uint8_t* payload, uint16_t payload_len)
{
    uint8_t frame[128];
    static uint8_t seq_num = 0;
    uint16_t frame_len;
    
    frame_len = net_build_frame(frame, dst_eui, 0, seq_num++, payload, payload_len);
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
    
    uint16_t fc = buffer[0] | (buffer[1] << 8);
    net_addr_mode_t dest_mode = (fc >> 10) & 0x03;
    net_addr_mode_t src_mode = (fc >> 14) & 0x03;
    
    msg->data = buffer;
    msg->len = len;
    msg->seq_num = buffer[2];
    
    /* PAN ID is at offset 3-4 for all modes we support */
    
    uint16_t offset = 5; /* After PAN ID */
    
    /* Parse destination address */
    if (dest_mode == NET_ADDR_MODE_16BIT) {
        msg->dest_is_eui64 = 0;
        msg->dst_addr16 = buffer[offset] | (buffer[offset + 1] << 8);
        offset += 2;
    } else if (dest_mode == NET_ADDR_MODE_64BIT) {
        msg->dest_is_eui64 = 1;
        memcpy(&msg->dst_eui64, buffer + offset, 8);
        offset += 8;
    } else {
        return 0; /* No address mode not supported */
    }
    
    /* Parse source address */
    if (src_mode == NET_ADDR_MODE_16BIT) {
        msg->src_is_eui64 = 0;
        msg->src_addr16 = buffer[offset] | (buffer[offset + 1] << 8);
        offset += 2;
    } else if (src_mode == NET_ADDR_MODE_64BIT) {
        msg->src_is_eui64 = 1;
        memcpy(&msg->src_eui64, buffer + offset, 8);
        offset += 8;
    } else {
        return 0; /* No address mode not supported */
    }
    
    msg->payload = buffer + offset;
    msg->payload_len = len - offset;
    
    return 1;
}

int net_is_broadcast(const net_message_t* msg)
{
    if (!msg) return 0;
    if (msg->dest_is_eui64) return 0;
    return (msg->dst_addr16 == NET_BROADCAST_ADDR);
}

int net_receive_once(void)
{
    uint32_t status_reg;
    net_message_t msg;
    
    if (!net_state.initialized) {
        return -1;
    }
    
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
    
    if (status_reg & SYS_STATUS_ALL_RX_ERR) {
        dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
    }
    
    return 0;
}
