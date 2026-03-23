#include <stdio.h>
#include <string.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "sleep.h"
#include "port.h"

/* Default communication configuration */
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

/* Frame control bytes */
#define FRAME_CTRL_16BIT_ADDR  0x8841  /* Data frame with 16-bit addressing */
#define PAN_ID 0xDECA
#define BROADCAST_ADDR_16  0xFFFF
#define SHORT_ADDR 0x0003  /* Our short address */
#define LONG_ADDR {0x03, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} /* 64-bit address */

/* Buffer for TX message */
#define TX_BUF_SIZE 128
static uint8 tx_buffer[TX_BUF_SIZE];

/* Function to build MAC header */
static void build_mac_header(uint8* buffer, uint16 frame_ctrl, uint8 seq_num, 
                             uint16 pan_id, uint16 dest_addr, uint16 src_addr)
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

/* Build broadcast frame */
int build_broadcast_frame(uint8* buffer, uint16 src_addr, uint8 seq_num, 
                          uint8* payload, uint8 payload_len)
{
    int header_len = 9;
    
    build_mac_header(buffer, FRAME_CTRL_16BIT_ADDR, seq_num, PAN_ID, 
                     BROADCAST_ADDR_16, src_addr);
    
    if (payload && payload_len > 0) {
        memcpy(buffer + header_len, payload, payload_len);
        return header_len + payload_len;
    }
    
    return header_len;
}

/* Send frame with DW1000 */
int send_frame(uint8* frame, uint16 frame_len, uint8 response_expected)
{
    uint32 status_reg;
    uint8 rx_buffer[128];
    
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
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXERR); // write 1 to clear
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
            uint16 frame_len_rx = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
            if (frame_len_rx <= sizeof(rx_buffer)) {
                dwt_readrxdata(rx_buffer, frame_len_rx, 0);
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

/* Send broadcast message */
int send_broadcast(uint16 src_addr, uint8* payload, uint8 payload_len)
{
    uint8 frame[TX_BUF_SIZE];
    static uint8 seq_num = 0;
    uint16 frame_len = build_broadcast_frame(frame, src_addr, seq_num++, payload, payload_len);
    
    return send_frame(frame, frame_len, 0);
}

/* Send broadcast and wait for response */
int send_broadcast_with_response(uint16 src_addr, uint8* payload, uint8 payload_len)
{
    uint8 frame[TX_BUF_SIZE];
    static uint8 seq_num = 0;
    uint16 frame_len = build_broadcast_frame(frame, src_addr, seq_num++, payload, payload_len);
    
    return send_frame(frame, frame_len, 1);
}

/* Initialize DW1000 */
static int init_dw1000(void)
{
    uint8 long_addr[8] = LONG_ADDR;
    
    reset_DW1000();
    
    spi_set_rate_low();
    if (dwt_initialise(DWT_LOADNONE) == DWT_ERROR) {
        return -1;
    }
    spi_set_rate_high();
    
    /* Configure DW1000 */
    dwt_configure(&config);
    dwt_setleds(DWT_LEDS_ENABLE);
    
    /* IMPORTANT: Set PAN ID and addresses for frame filtering */
    dwt_setpanid(PAN_ID);           /* Set PAN ID to match broadcast messages */
    dwt_setaddress16(SHORT_ADDR);   /* Set our 16-bit short address */
    dwt_seteui(long_addr);           /* Set our 64-bit extended address */
    
    /* Enable frame filtering to accept data frames */
    /* DWT_FF_DATA_EN - accept data frames
     * DWT_FF_COORD_EN - allow receiving frames with no destination address */
    dwt_enableframefilter(DWT_FF_DATA_EN);
    
    return 0;
}

#ifdef TX_BROADCAST_PROGRAM
int main(void)
#else
int tx_broadcast_program(void)
#endif
{
    uint32 counter = 0;
    uint8 ping_payload[] = {'P', 'I', 'N', 'G'};
    uint8 discovery_payload[] = {'D', 'I', 'S', 'C', 'O', 'V', 'E', 'R'};
    
    /* Initialize hardware */
    peripherals_init();
    
    /* Initialize DW1000 */
    if (init_dw1000() != 0) {
        while (1) { /* Error - blink LED or something */ }
    }
    
    /* Main loop */
    while (1)
    {
        /* Send simple broadcast message */
//        send_broadcast(0x0001, ping_payload, sizeof(ping_payload));
//        sleep_ms(1000);
        
        /* Send broadcast with response expected */
        send_broadcast_with_response(0x0001, discovery_payload, sizeof(discovery_payload));
        sleep_ms(2000);
        
        counter++;
    }
    
    return 0;
}
