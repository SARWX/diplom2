#ifndef NET_MAC_H
#define NET_MAC_H

#include <stdint.h>
#include <stddef.h>

/*==============================================================================
 * MAC Layer Configuration
 *============================================================================*/

/* PAN ID and addressing */
#define NET_PAN_ID              0xDECA
#define NET_BROADCAST_ADDR      0xFFFF

/* Frame control types */
#define NET_FRAME_CTRL_16BIT    0x8841  /* Data frame with 16-bit addressing */
#define NET_FRAME_CTRL_64BIT    0x8C41  /* Data frame with 16-bit src, 64-bit dest */

/* Frame filtering options */
typedef enum {
    NET_FILTER_NONE      = 0x0000,
    NET_FILTER_COORD     = 0x0002,
    NET_FILTER_BEACON    = 0x0004,
    NET_FILTER_DATA      = 0x0008,
    NET_FILTER_ACK       = 0x0010,
    NET_FILTER_MAC       = 0x0020,
    NET_FILTER_RESERVED  = 0x0040,
    NET_FILTER_ALL_DATA  = NET_FILTER_DATA | NET_FILTER_ACK | NET_FILTER_MAC
} net_filter_mask_t;

/*==============================================================================
 * Data Types
 *============================================================================*/

/* 64-bit EUI address */
typedef struct {
    uint8_t bytes[8];
} net_eui64_t;

/* 16-bit short address */
typedef uint16_t net_addr16_t;

/* Network message structure */
typedef struct {
    uint8_t* data;          /* Pointer to message data (including MAC header) */
    uint16_t len;           /* Total message length */
    uint8_t seq_num;        /* Sequence number */
    net_addr16_t src_addr;  /* Source address */
    net_addr16_t dst_addr;  /* Destination address */
    uint8_t* payload;       /* Pointer to payload (after MAC header) */
    uint16_t payload_len;   /* Payload length */
} net_message_t;

/* Callback for received messages */
typedef void (*net_rx_callback_t)(net_message_t* msg);

/*==============================================================================
 * Initialization and Configuration
 *============================================================================*/

/**
 * Initialize MAC layer
 * @param short_addr - 16-bit short address for this device
 * @param eui64 - 64-bit EUI address for this device (can be NULL)
 * @param filter_mask - frame filtering mask (use NET_FILTER_*)
 * @return 0 on success, -1 on error
 */
int net_init(net_addr16_t short_addr, const net_eui64_t* eui64, uint16_t filter_mask);

/**
 * Set receive callback
 * @param callback - function to call when message received
 */
void net_set_rx_callback(net_rx_callback_t callback);

/**
 * Get current short address
 * @return current short address
 */
net_addr16_t net_get_short_addr(void);

/*==============================================================================
 * Message Building
 *============================================================================*/

/**
 * Build broadcast message
 * @param buffer - output buffer
 * @param src_addr - source address
 * @param seq_num - sequence number
 * @param payload - payload data
 * @param payload_len - payload length
 * @return total frame length
 */
uint16_t net_build_broadcast(uint8_t* buffer, net_addr16_t src_addr, uint8_t seq_num,
                              const uint8_t* payload, uint16_t payload_len);

/**
 * Build unicast message with 16-bit addressing
 * @param buffer - output buffer
 * @param dst_addr - destination address
 * @param src_addr - source address
 * @param seq_num - sequence number
 * @param payload - payload data
 * @param payload_len - payload length
 * @return total frame length
 */
uint16_t net_build_unicast(uint8_t* buffer, net_addr16_t dst_addr, net_addr16_t src_addr,
                            uint8_t seq_num, const uint8_t* payload, uint16_t payload_len);

/**
 * Build response message (16-bit src, 64-bit dest)
 * @param buffer - output buffer
 * @param dst_eui - destination 64-bit address
 * @param src_addr - source 16-bit address
 * @param seq_num - sequence number
 * @param payload - payload data
 * @param payload_len - payload length
 * @return total frame length
 */
uint16_t net_build_response(uint8_t* buffer, const net_eui64_t* dst_eui,
                             net_addr16_t src_addr, uint8_t seq_num,
                             const uint8_t* payload, uint16_t payload_len);

/*==============================================================================
 * Transmission Functions
 *============================================================================*/

/**
 * Send a message (raw frame)
 * @param frame - frame data (including MAC header)
 * @param frame_len - frame length
 * @param response_expected - whether to wait for response
 * @return 1 if response received, 0 if no response, -1 on error
 */
int net_send_frame(uint8_t* frame, uint16_t frame_len, uint8_t response_expected);

/**
 * Send broadcast message
 * @param src_addr - source address
 * @param payload - payload data
 * @param payload_len - payload length
 * @return 1 on success, -1 on error
 */
int net_send_broadcast(net_addr16_t src_addr, const uint8_t* payload, uint16_t payload_len);

/**
 * Send broadcast and wait for response
 * @param src_addr - source address
 * @param payload - payload data
 * @param payload_len - payload length
 * @return 1 if response received, 0 if no response, -1 on error
 */
int net_send_broadcast_with_response(net_addr16_t src_addr, const uint8_t* payload, uint16_t payload_len);

/**
 * Send unicast message
 * @param dst_addr - destination address
 * @param src_addr - source address
 * @param payload - payload data
 * @param payload_len - payload length
 * @return 1 on success, -1 on error
 */
int net_send_unicast(net_addr16_t dst_addr, net_addr16_t src_addr,
                      const uint8_t* payload, uint16_t payload_len);

/*==============================================================================
 * Reception Functions
 *============================================================================*/

/**
 * Parse received message
 * @param buffer - received frame data
 * @param len - frame length
 * @param msg - output message structure (points into buffer)
 * @return 1 if valid, 0 if invalid
 */
int net_parse_message(uint8_t* buffer, uint16_t len, net_message_t* msg);

/**
 * Check if message is broadcast
 * @param msg - parsed message
 * @return 1 if broadcast, 0 otherwise
 */
int net_is_broadcast(const net_message_t* msg);

/**
 * Run receiver loop (non-blocking)
 * @return 1 if message received and processed, 0 otherwise
 */
int net_receive_once(void);

/**
 * Run receiver loop (blocking)
 * @param timeout_ms - timeout in milliseconds (0 = infinite)
 * @return 1 if message received, 0 on timeout, -1 on error
 */
int net_receive_blocking(uint32_t timeout_ms);

/*==============================================================================
 * Utility Functions
 *============================================================================*/

/**
 * Convert 16-bit address to string
 * @param addr - address to convert
 * @param str - output buffer (at least 6 bytes)
 */
void net_addr16_to_str(net_addr16_t addr, char* str);

/**
 * Convert 64-bit EUI to string
 * @param eui - EUI to convert
 * @param str - output buffer (at least 24 bytes)
 */
void net_eui64_to_str(const net_eui64_t* eui, char* str);

/**
 * Convert string to 16-bit address
 * @param str - string (e.g., "0x1234")
 * @return parsed address
 */
net_addr16_t net_str_to_addr16(const char* str);

#endif /* NET_MAC_H */
