#ifndef NET_DEVICES_H
#define NET_DEVICES_H

#include <stdint.h>
#include "device_id.h"

#define MAX_ANCHORS 16
#define MAX_DISTANCES MAX_ANCHORS
#define MAC_ADDR_LEN 6

/** @brief Sentinel value meaning "distance not yet measured". */
#define DISTANCE_INVALID (-3.4028235E+38f)

/**
 * @brief A single network node entry in the discovered-devices linked list.
 */
typedef struct net_device {
	uint8_t mac_address[6];       /**< MAC address (6 bytes, from EUI-64 or short addr) */
	uint8_t seq_id;               /**< Sequential ID assigned during enumeration */
	device_type_t device_type;    /**< Type of this node (anchor or tag) */
	float* distances;             /**< Array[MAX_ANCHORS] of measured distances to other anchors (m) */
	struct net_device* next;      /**< Next node in the singly-linked list */
} net_device_t;

/**
 * @brief Head of the discovered-devices list with aggregate metadata.
 */
typedef struct {
	net_device_t* head;     /**< First node in the list */
	uint8_t total_anchors;  /**< Number of anchors currently in the list */
	uint8_t initialized;    /**< Non-zero after a successful enumeration */
} net_devices_list_t;

/* Initialization */
void net_devices_init(net_devices_list_t* list);

/* List management */
net_device_t* net_device_create(const uint8_t* mac, device_type_t device_type);
int net_device_add(net_devices_list_t* list, net_device_t* device);
void net_device_remove(net_devices_list_t* list, uint8_t seq_id);
void net_device_remove_by_mac(net_devices_list_t* list, const uint8_t* mac);
void net_devices_clear(net_devices_list_t* list);

/* Lookup */
net_device_t* net_device_find_by_seq(net_devices_list_t* list, uint8_t seq_id);
net_device_t* net_device_find_by_mac(net_devices_list_t* list, const uint8_t* mac);

/* Debug */
void net_devices_print(net_devices_list_t* list);

/* Distance tracking */
void net_device_update_distance(net_device_t* device, uint8_t to_seq_id, float distance);

/*==============================================================================
 * Device List Serialization
 *
 * Binary format (little-endian):
 *
 *   Offset        Size  Field
 *   ----------    ----  -------------------------------------------
 *   0             1     magic[0] = 0xAA
 *   1             1     magic[1] = 0xCC
 *   2             1     version  = 1
 *   3             1     device_count
 *   4 + 8*i       1     seq_id
 *   5 + 8*i       1     device_type  (device_type_t, see below)
 *   6 + 8*i       6     mac_address (6 bytes, big-endian order)
 *
 *   Total: 4 + device_count * 8 bytes.
 *
 *   device_type values: 0=NONE 1=MAIN_ANCHOR 2=ANCHOR 3=TAG
 *============================================================================*/

#define DEV_LIST_MAGIC_0   0xAAu
#define DEV_LIST_MAGIC_1   0xCCu
#define DEV_LIST_VERSION   1u
#define DEV_LIST_HDR_SIZE  4u
#define DEV_LIST_ROW_SIZE  8u   /* seq_id(1) + device_type(1) + mac(6) */

/**
 * @brief Serialize the device list into @p buf.
 *
 * @param list  Source device list.
 * @param buf   Output buffer. Must be at least
 *              DEV_LIST_HDR_SIZE + DEV_LIST_ROW_SIZE * list->total_anchors bytes.
 * @param len   Set to the number of bytes written.
 */
void net_devices_serialize(const net_devices_list_t* list,
                            uint8_t* buf, uint16_t* len);

#endif /* NET_DEVICES_H */
