#ifndef MAIN_ANCHOR_H
#define MAIN_ANCHOR_H

#include <stdint.h>
#include "device_id.h"

#define MAX_ANCHORS 16
#define MAX_DISTANCES MAX_ANCHORS

#define LISTEN_AFTR_BROADCAST_MS 2000

// Broadcast исследование окружения
#define DISCOVERY_PAYLOAD "DISCOVER"
#define DISCOVERY_PAYLOAD_LEN (sizeof(DISCOVERY_PAYLOAD) - 1)

typedef struct anchor {
    uint8_t mac_address[6];
    uint8_t seq_id; // anchor / tag
    uint8_t device_type;
    float* distances;
    struct anchor* next;
} net_device_t;

typedef struct {
    net_device_t* head;
    uint8_t total_anchors;
    uint8_t initialized;
} net_devices_list_t;

/* Anchor list management */
net_device_t* anchor_create(const uint8_t* mac, uint8_t seq_id);
void anchor_add(net_devices_list_t* ctx, net_device_t* new_anchor);
void anchor_remove(net_devices_list_t* ctx, uint8_t seq_id);
net_device_t* anchor_find_by_seq(net_devices_list_t* ctx, uint8_t seq_id);
net_device_t* anchor_find_by_mac(net_devices_list_t* ctx, const uint8_t* mac);
void anchor_free_all(net_devices_list_t* ctx);
void anchor_print_list(net_devices_list_t* ctx);
void anchor_update_distance(net_device_t* from, uint8_t to_seq_id, float distance);

/* System management */
int system_init(net_devices_list_t* ctx, const uint8_t* my_mac);
int system_enumerate(net_devices_list_t* ctx);
int system_configure(net_devices_list_t* ctx);

/* Device interface */
void main_anchor_init(device_config_t* dev);
void main_anchor_loop(device_config_t* dev);

#endif /* MAIN_ANCHOR_H */
