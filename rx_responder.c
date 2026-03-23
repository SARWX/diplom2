#include <stdio.h>
#include <string.h>
#include "deca_device_api.h"
#include "deca_regs.h"
#include "port.h"

/* Example application name */
#define APP_NAME "RX RESPONDER v1.0"

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

/* PAN ID and Address configuration */
#define PAN_ID 0xDECA
#define SHORT_ADDR 0x0002  /* Our short address */
#define LONG_ADDR {0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00} /* 64-bit address */

/* Response message frame (21 bytes as in example) */
static uint8 tx_msg[] = {
    0x41, 0x8C,           /* Frame control */
    0,                    /* Sequence number */
    0x9A, 0x60,           /* Application ID */
    0, 0, 0, 0, 0, 0, 0, 0, /* Destination address (64-bit) */
    0x02, 0x00,           /* Source address (16-bit) - 0x0002 */
    0x10,                 /* Function code */
    0x00,                 /* Activity code */
    0, 0,                 /* Blink rate */
    0, 0                  /* FCS (will be auto-filled) */
};

/* Indexes for accessing fields in response message */
#define RESPONSE_SN_IDX 2
#define RESPONSE_DEST_IDX 5

/* Broadcast frame format (as sent by tx_broadcast) */
#define BROADCAST_SRC_IDX 7    /* Source address starts at byte 7 */
#define BROADCAST_PAYLOAD_IDX 9 /* Payload starts at byte 9 */

/* Buffer for received frames */
#define FRAME_LEN_MAX 127
static uint8 rx_buffer[FRAME_LEN_MAX];
static uint32 status_reg = 0;
static uint16 frame_len = 0;
static uint8 response_seq = 0;

/* Function to check if received frame is a valid broadcast */
static int is_valid_broadcast(uint8* buffer, uint16 len)
{
    if (len < 10) return 0;
    
    /* Check frame control (0x8841) */
    if (buffer[0] != 0x41 || buffer[1] != 0x88) return 0;
    
    /* Check PAN ID (0xDECA) */
    if (buffer[3] != 0xCA || buffer[4] != 0xDE) return 0;
    
    /* Check destination address is broadcast (0xFFFF) */
    if (buffer[5] != 0xFF || buffer[6] != 0xFF) return 0;
    
    return 1;
}

/* Function to extract source address from broadcast */
static uint16 get_broadcast_source(uint8* buffer)
{
    return (buffer[BROADCAST_SRC_IDX] | (buffer[BROADCAST_SRC_IDX + 1] << 8));
}

/* Function to check payload content */
static int is_discovery_payload(uint8* buffer, uint16 len)
{
    if (len < BROADCAST_PAYLOAD_IDX + 4) return 0;
    
    /* Check for "DISCOVER" payload */
    if (len >= BROADCAST_PAYLOAD_IDX + 6 &&
        memcmp(&buffer[BROADCAST_PAYLOAD_IDX], "DISCOV", 6) == 0) return 1;
    
    /* Check for "PING" payload */
    if (memcmp(&buffer[BROADCAST_PAYLOAD_IDX], "PING", 4) == 0) return 1;
    
    return 0;
}

/* Function to build response to broadcast */
static void build_response(uint16 source_addr)
{
    /* Increment sequence number */
    response_seq++;
    tx_msg[RESPONSE_SN_IDX] = response_seq;
    
    /* Set destination address (64-bit) based on source address from broadcast */
    memset(&tx_msg[RESPONSE_DEST_IDX], 0, 8);
    tx_msg[RESPONSE_DEST_IDX] = source_addr & 0xFF;
    tx_msg[RESPONSE_DEST_IDX + 1] = (source_addr >> 8) & 0xFF;
}

/* Function to send response */
static int send_response(void)
{
    uint32 status;
    
    /* Write response to DW1000 */
    dwt_writetxdata(sizeof(tx_msg), tx_msg, 0);
    dwt_writetxfctrl(sizeof(tx_msg), 0, 0);
    
    /* Start transmission immediately */
    dwt_starttx(DWT_START_TX_IMMEDIATE);
    
    /* Wait for transmission to complete */
    while (!((status = dwt_read32bitreg(SYS_STATUS_ID)) & SYS_STATUS_TXFRS)) {
        if (status & SYS_STATUS_TXERR) {
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXERR);
            return -1;
        }
    }
    
    /* Clear TX frame sent event */
    dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_TXFRS);
    
    return 0;
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

#ifdef RX_BROADCAST_PROGRAM
int main(void)
#else
int rx_broadcast_program(void)
#endif
{
    /* Initialize hardware */
    peripherals_init();
    
    /* Initialize DW1000 */
    if (init_dw1000() != 0) {
        while (1) { /* Error state */ }
    }
    
    /* Main loop */
    while (1)
    {
        /* Disable RX timeout for continuous listening */
        dwt_setrxtimeout(0);
        
        /* Enable receiver */
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
        
        /* Wait for frame reception or error */
        while (!((status_reg = dwt_read32bitreg(SYS_STATUS_ID)) & 
                 (SYS_STATUS_RXFCG | SYS_STATUS_ALL_RX_ERR))) {
            /* Wait */
        }
        
        if (status_reg & SYS_STATUS_RXFCG)
        {
            /* Good frame received */
            frame_len = dwt_read32bitreg(RX_FINFO_ID) & RX_FINFO_RXFL_MASK_1023;
            
            if (frame_len <= FRAME_LEN_MAX) {
                dwt_readrxdata(rx_buffer, frame_len, 0);
                
                /* Check if this is a broadcast message */
                if (is_valid_broadcast(rx_buffer, frame_len)) {
                    uint16 src_addr = get_broadcast_source(rx_buffer);
                    
                    /* Check payload to decide if we should respond */
                    if (is_discovery_payload(rx_buffer, frame_len)) {
                        /* Build and send response */
                        build_response(src_addr);
                        send_response();
                    }
                }
            }
            
            /* Clear RX good frame event */
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_RXFCG);
        }
        else
        {
            /* Clear RX error events */
            dwt_write32bitreg(SYS_STATUS_ID, SYS_STATUS_ALL_RX_ERR);
        }
    }
    
    return 0;
}
