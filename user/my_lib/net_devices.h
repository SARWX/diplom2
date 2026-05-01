#ifndef NET_DEVICES_H
#define NET_DEVICES_H

#include <stdint.h>
#include "device_id.h"

#define MAX_ANCHORS 16
#define MAX_DISTANCES MAX_ANCHORS
#define MAC_ADDR_LEN 6

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

void net_devices_set_debug(uint8_t enable);

#endif /* NET_DEVICES_H */
