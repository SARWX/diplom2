#ifndef NET_MAC_H
#define NET_MAC_H

#include <stdint.h>
#include <stddef.h>
#include "deca_device_api.h"

/*==============================================================================
 * MAC Layer Configuration
 *============================================================================*/

#define NET_PAN_ID              0xDECA
#define NET_BROADCAST_ADDR      0xFFFF
#define MAX_PAYLOAD_SIZE        116
#define NET_FCS_LEN             2  /* Frame Check Sequence length (CRC-16) */


/*==============================================================================
 * Data Types
 *============================================================================*/

/* 64-bit EUI address */
typedef struct {
	uint8_t bytes[8];
} net_eui64_t;

/* 16-bit short address */
typedef uint16_t net_addr16_t;

/* Addressing mode for source */
typedef enum {
	NET_ADDR_MODE_NONE = 0,
	NET_ADDR_MODE_16BIT = 2,
	NET_ADDR_MODE_64BIT = 3
} net_addr_mode_t;

/* Parsed message structure */
typedef struct {
	uint8_t* data;              /* Raw frame data */
	uint16_t len;               /* Total frame length */
	uint8_t seq_num;            /* Sequence number */
	
	/* Destination */
	uint8_t dest_is_eui64;      /* 1 if 64-bit, 0 if 16-bit */
	union {
		net_addr16_t dst_addr16;
		net_eui64_t dst_eui64;
	};
	
	/* Source */
	uint8_t src_is_eui64;       /* 1 if 64-bit, 0 if 16-bit */
	union {
		net_addr16_t src_addr16;
		net_eui64_t src_eui64;
	};
	
	uint8_t* payload;           /* Pointer to payload */
	uint16_t payload_len;       /* Payload length */
} net_message_t;

typedef enum {
	NET_MODE_IDLE,
	NET_MODE_ENUMERATION,
	NET_MODE_SYNC_WAIT,
	NET_MODE_CONFIG,
	NET_MODE_RANGING
} net_mode_t;

typedef struct {
	uint8_t initialized;
	uint8_t use_eui64;
	net_addr16_t short_addr;
	net_eui64_t eui64;
	uint8_t rx_buffer[128];
	net_mode_t mode;
} net_state_t;

/* global net state */
extern net_state_t net_state;

/*==============================================================================
 * Initialization
 *============================================================================*/

/**
 * Initialize MAC layer
 * @param use_eui64 - if 1, use 64-bit addressing for TX, else use 16-bit
 * @param short_addr - 16-bit short address (used if use_eui64=0 or as fallback)
 * @param eui64 - 64-bit EUI address (used if use_eui64=1)
 * @param filter_mask - frame filtering mask
 * @return 0 on success, -1 on error
 */
int net_init(int use_eui64, net_eui64_t* eui64, uint16_t filter_mask);
/**
 * Get current source address (as 16-bit)
 */
net_addr16_t net_get_src_addr16(void);

/**
 * Get current source address (as 64-bit)
 */
const net_eui64_t* net_get_src_eui64(void);

/**
 * Get current addressing mode
 */
int net_use_eui64(void);

/*==============================================================================
 * Frame Building Helpers
 *============================================================================*/

/**
 * Build frame control field
 * @param dest_mode - destination addressing mode
 * @param src_mode - source addressing mode
 * @param pan_id_compression - PAN ID compression flag
 * @param ack_request - ACK request flag
 * @return 16-bit frame control value
 */
uint16_t net_build_frame_control(net_addr_mode_t dest_mode, net_addr_mode_t src_mode,
								  int pan_id_compression, int ack_request);

/**
 * Build MAC header (only header, no payload)
 * @param buffer - output buffer
 * @param dest_eui - 64-bit destination (if NULL, use dest_addr16)
 * @param dest_addr16 - 16-bit destination (used if dest_eui is NULL)
 * @param seq_num - sequence number
 * @return header length
 */
uint16_t net_build_header(uint8_t* buffer,
						  const net_eui64_t* dest_eui, net_addr16_t dest_addr16,
						  uint8_t seq_num);

/**
 * Build complete frame (header + payload)
 */
uint16_t net_build_frame(uint8_t* buffer,
						 const net_eui64_t* dest_eui, net_addr16_t dest_addr16,
						 uint8_t seq_num,
						 const uint8_t* payload, uint16_t payload_len);

/*==============================================================================
 * Transmission
 *============================================================================*/

/**
 * Send a raw frame
 */
int net_send_frame(uint8_t* frame, uint16_t frame_len, uint8_t response_expected);

/**
 * Send broadcast message (uses current device's source address)
 */
int net_send_broadcast(const uint8_t* payload, uint16_t payload_len);

/**
 * Send broadcast and wait for response
 */
int net_send_broadcast_with_response(const uint8_t* payload, uint16_t payload_len);

/**
 * Send to 16-bit address
 */
int net_send_to_16bit(net_addr16_t dst_addr, const uint8_t* payload, uint16_t payload_len);

/**
 * Send to 64-bit address
 */
int net_send_to_64bit(const net_eui64_t* dst_eui, const uint8_t* payload, uint16_t payload_len);

/*==============================================================================
 * Reception
 *============================================================================*/

/**
 * Parse received message (auto-detects frame format)
 */
int net_parse_message(uint8_t* buffer, uint16_t len, net_message_t* msg);

/**
 * Check if message is broadcast
 */
int net_is_broadcast(const net_message_t* msg);

#endif /* NET_MAC_H */
