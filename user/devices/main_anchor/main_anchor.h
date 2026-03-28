#ifndef MAIN_ANCHOR_H
#define MAIN_ANCHOR_H

#include <stdint.h>

#define MAX_ANCHORS 16
#define MAX_DISTANCES MAX_ANCHORS

typedef struct anchor {
    uint8_t mac_address[6];
    uint8_t seq_id;
    float* distances;
    struct anchor* next;
} anchor_t;

typedef struct {
    anchor_t* head;
    uint8_t total_anchors;
    uint8_t my_seq_id;
    uint8_t initialized;
    uint8_t my_mac[6];
} system_context_t;

/* Anchor list management */
anchor_t* anchor_create(const uint8_t* mac, uint8_t seq_id);
void anchor_add(system_context_t* ctx, anchor_t* new_anchor);
void anchor_remove(system_context_t* ctx, uint8_t seq_id);
anchor_t* anchor_find_by_seq(system_context_t* ctx, uint8_t seq_id);
anchor_t* anchor_find_by_mac(system_context_t* ctx, const uint8_t* mac);
void anchor_free_all(system_context_t* ctx);
void anchor_print_list(system_context_t* ctx);
void anchor_update_distance(anchor_t* from, uint8_t to_seq_id, float distance);

/* System management */
int system_init(system_context_t* ctx, const uint8_t* my_mac);
int system_enumerate(system_context_t* ctx);
int system_configure(system_context_t* ctx);

/* Device interface */
void main_anchor_init(device_config_t* dev);
void main_anchor_loop(device_config_t* dev);

/* UART callback */
void uart_line_callback(const char* line, uint16_t len);

#endif /* MAIN_ANCHOR_H */
