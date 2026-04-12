#ifndef NET_DEVICES_H
#define NET_DEVICES_H

#include <stdint.h>
#include "device_id.h"

#define MAX_ANCHORS 16
#define MAX_DISTANCES MAX_ANCHORS
#define MAC_ADDR_LEN 6

typedef struct net_device {
	uint8_t mac_address[6];
	uint8_t seq_id;
	device_type_t device_type;
	float* distances;
	struct net_device* next;
} net_device_t;

typedef struct {
	net_device_t* head;
	uint8_t total_anchors;
	uint8_t initialized;
} net_devices_list_t;

/* Инициализация */
void net_devices_init(net_devices_list_t* list);

/* Управление списком */
net_device_t* net_device_create(const uint8_t* mac, uint8_t seq_id, device_type_t device_type);
int net_device_add(net_devices_list_t* list, net_device_t* device);
void net_device_remove(net_devices_list_t* list, uint8_t seq_id);
void net_device_remove_by_mac(net_devices_list_t* list, const uint8_t* mac);
void net_devices_clear(net_devices_list_t* list);

/* Поиск */
net_device_t* net_device_find_by_seq(net_devices_list_t* list, uint8_t seq_id);
net_device_t* net_device_find_by_mac(net_devices_list_t* list, const uint8_t* mac);

/* Вывод */
void net_devices_print(net_devices_list_t* list);

/* Расстояния */
void net_device_update_distance(net_device_t* device, uint8_t to_seq_id, float distance);

void net_devices_set_debug(uint8_t enable);

#endif /* NET_DEVICES_H */
