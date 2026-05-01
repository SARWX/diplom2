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

/** @brief 64-bit Extended Unique Identifier (EUI-64) of a network node. */
typedef struct {
	uint8_t bytes[8]; /**< Address bytes, LSB first */
} net_eui64_t;

/** @brief 16-bit IEEE 802.15.4 short address. */
typedef uint16_t net_addr16_t;

/** @brief MAC frame addressing mode (IEEE 802.15.4). */
typedef enum {
	NET_ADDR_MODE_NONE  = 0, /**< No address field */
	NET_ADDR_MODE_16BIT = 2, /**< 16-bit short address */
	NET_ADDR_MODE_64BIT = 3  /**< 64-bit EUI address */
} net_addr_mode_t;

/** @brief Parsed network message produced by net_parse_message(). */
typedef struct {
	uint8_t* data;         /**< Raw frame buffer (including MAC header) */
	uint16_t len;          /**< Total frame length in bytes */
	uint8_t  seq_num;      /**< MAC sequence number */

	uint8_t dest_is_eui64; /**< Non-zero if destination address is EUI-64 */
	union {
		net_addr16_t dst_addr16;
		net_eui64_t  dst_eui64;
	};

	uint8_t src_is_eui64;  /**< Non-zero if source address is EUI-64 */
	union {
		net_addr16_t src_addr16;
		net_eui64_t  src_eui64;
	};

	uint8_t*  payload;     /**< Pointer to payload within data buffer */
	uint16_t  payload_len; /**< Payload length in bytes */
} net_message_t;

/** @brief Network layer operational mode. */
typedef enum {
	NET_MODE_IDLE,        /**< Idle, waiting for activity */
	NET_MODE_ENUMERATION, /**< Device enumeration in progress */
	NET_MODE_SYNC_WAIT,   /**< Waiting for sync-list acknowledgements */
	NET_MODE_CONFIG,      /**< Distance measurement configuration phase */
	NET_MODE_RANGING      /**< Continuous ranging mode */
} net_mode_t;

/** @brief Global MAC layer state. */
typedef struct {
	uint8_t initialized;            /**< Non-zero after net_init() succeeds */
	uint8_t use_eui64;              /**< Non-zero to use EUI-64 addressing in TX frames */
	net_addr16_t short_addr;        /**< This node's 16-bit short address */
	net_eui64_t  eui64;             /**< This node's EUI-64 address */
	uint8_t rx_buffer[128];         /**< Working buffer for frame parsing (main loop only) */
	volatile net_mode_t mode;       /**< Current operational mode */
} net_state_t;

/** @brief Global instance of the MAC layer state, shared across all modules. */
extern net_state_t net_state;

/**
 * @brief Common top-half handler for DW1000 RX-OK interrupt.
 *
 * Reads the received frame from DW1000 and pushes it into the ring buffer.
 * Call this from every device's rx_ok_cb — never do more work in the ISR.
 */
void net_rx_ok_isr(const dwt_cb_data_t *cb_data);
/** @brief Shared RX-timeout callback — re-arms receiver. */
void net_rx_to_isr(const dwt_cb_data_t *cb_data);
/** @brief Shared RX-error callback — re-arms receiver. */
void net_rx_err_isr(const dwt_cb_data_t *cb_data);

/**
 * @brief Pop one frame from the ring buffer into msg (bottom half).
 *
 * Returns 1 and fills msg if a frame was available, 0 otherwise.
 */
int net_rx_poll(net_message_t *msg);

/*==============================================================================
 * Initialization
 *============================================================================*/

/**
 * @brief Initialize MAC layer.
 * @param use_eui64 - if 1, use EUI-64 addressing for TX; if 0, use 16-bit short address
 * @param eui64 - this node's EUI-64 address
 * @param filter_mask - DW1000 frame filtering mask (e.g. DWT_FF_DATA_EN)
 * @return 0 on success, -1 on error
 */
int net_init(int use_eui64, net_eui64_t* eui64, uint16_t filter_mask);

/** @brief Get this node's 16-bit short address. */
net_addr16_t net_get_src_addr16(void);
/** @brief Get this node's EUI-64 address. */
const net_eui64_t* net_get_src_eui64(void);
/** @brief Returns non-zero if this node uses EUI-64 addressing. */
int net_use_eui64(void);

/*==============================================================================
 * Frame Building Helpers
 *============================================================================*/

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
uint16_t net_build_header(uint8_t* buffer, const net_eui64_t* dest_eui, 
				net_addr16_t dest_addr16, uint8_t seq_num);

/**
 * Build complete frame (header + payload)
 */
uint16_t net_build_frame(uint8_t* buffer, const net_eui64_t* dest_eui,
				net_addr16_t dest_addr16, uint8_t seq_num,
				const uint8_t* payload, uint16_t payload_len);

/*==============================================================================
 * Transmission
 *============================================================================*/

int net_send_frame(uint8_t* frame, uint16_t frame_len, uint8_t response_expected);
int net_send_frame_ranging(uint8_t* frame, uint16_t frame_len, uint8_t response_expected);

/**
 * Send broadcast message (uses current device's source address)
 */
int net_send_broadcast(const uint8_t* payload, uint16_t payload_len);

/**
 * Send broadcast and wait for response
 */
int net_send_broadcast_with_response(const uint8_t* payload,
					uint16_t payload_len);

/**
 * Send to 16-bit address
 */
int net_send_to_16bit(net_addr16_t dst_addr, const uint8_t* payload,
						uint16_t payload_len);

/**
 * Send to 64-bit address
 */
int net_send_to_64bit(const net_eui64_t* dst_eui, const uint8_t* payload,
						uint16_t payload_len);

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
