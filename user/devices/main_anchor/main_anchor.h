#ifndef MAIN_ANCHOR_H
#define MAIN_ANCHOR_H

#include <stdint.h>
#include "device_id.h"

#define MAX_ANCHORS 16
#define MAX_DISTANCES MAX_ANCHORS

#define LISTEN_AFTR_BROADCAST_MS 2000

#define MAC_ADDR_LEN 6

// Broadcast исследование окружения
#define DISCOVER_PAYLOAD ((const uint8_t*)"DISCOVER")
#define DISCOVER_PAYLOAD_LEN (sizeof("DISCOVER") - 1)

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

void main_anchor_init(void);
void main_anchor_loop(void);

#endif /* MAIN_ANCHOR_H */
