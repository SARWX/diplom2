#include "net_devices.h"
#include "uart.h"
#include <string.h>

static uint8_t debug_enabled = 0;

static net_device_t device_pool[MAX_ANCHORS];
static float        dist_pool[MAX_ANCHORS][MAX_DISTANCES];
static uint8_t      device_used[MAX_ANCHORS];

void net_devices_set_debug(uint8_t enable)
{
	debug_enabled = enable;
}

void net_devices_init(net_devices_list_t* list)
{
	if (!list) return;
	list->head = NULL;
	list->total_anchors = 0;
	list->initialized = 0;
}

static void pool_free(net_device_t* device)
{
	for (int i = 0; i < MAX_ANCHORS; i++) {
		if (&device_pool[i] == device) {
			device_used[i] = 0;
			return;
		}
	}
}

net_device_t* net_device_create(const uint8_t* mac, uint8_t seq_id, device_type_t device_type)
{
	int slot = -1;
	for (int i = 0; i < MAX_ANCHORS; i++) {
		if (!device_used[i]) { slot = i; break; }
	}
	if (slot < 0) {
		uart_puts("ERROR: device pool full\r\n");
		return NULL;
	}

	device_used[slot] = 1;
	net_device_t* device = &device_pool[slot];

	memcpy(device->mac_address, mac, MAC_ADDR_LEN);
	device->seq_id      = seq_id;
	device->device_type = device_type;
	device->next        = NULL;
	device->distances   = dist_pool[slot];

	for (int i = 0; i < MAX_DISTANCES; i++)
		device->distances[i] = -1.0f;

	if (debug_enabled)
		uart_printf("Created device: seq_id=%d, MAC=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
			    seq_id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

	return device;
}

int net_device_add(net_devices_list_t* list, net_device_t* device)
{
	if (!list || !device) return -1;

	if (net_device_find_by_mac(list, device->mac_address)) {
		uart_puts("ERROR: Device with MAC already exists\r\n");
		pool_free(device);
		return -2;
	}

	if (device->seq_id == 0)
		device->seq_id = list->total_anchors + 1;
	device->next = list->head;
	list->head = device;
	list->total_anchors++;

	if (debug_enabled)
		uart_printf("Added device seq_id=%d, total=%d\r\n",
			    device->seq_id, list->total_anchors);

	return device->seq_id;
}

void net_device_remove(net_devices_list_t* list, uint8_t seq_id)
{
	if (!list || !list->head) return;

	net_device_t* current = list->head;
	net_device_t* prev = NULL;

	while (current) {
		if (current->seq_id == seq_id) {
			if (prev)
				prev->next = current->next;
			else
				list->head = current->next;
			pool_free(current);
			list->total_anchors--;
			return;
		}
		prev = current;
		current = current->next;
	}
}

void net_device_remove_by_mac(net_devices_list_t* list, const uint8_t* mac)
{
	if (!list || !list->head) return;

	net_device_t* current = list->head;
	net_device_t* prev = NULL;

	while (current) {
		if (memcmp(current->mac_address, mac, MAC_ADDR_LEN) == 0) {
			if (prev)
				prev->next = current->next;
			else
				list->head = current->next;
			pool_free(current);
			list->total_anchors--;
			return;
		}
		prev = current;
		current = current->next;
	}
}

void net_devices_clear(net_devices_list_t* list)
{
	if (!list) return;

	net_device_t* current = list->head;
	while (current) {
		net_device_t* next = current->next;
		pool_free(current);
		current = next;
	}
	list->head = NULL;
	list->total_anchors = 0;
}

net_device_t* net_device_find_by_seq(net_devices_list_t* list, uint8_t seq_id)
{
	if (!list) return NULL;

	net_device_t* current = list->head;
	while (current) {
		if (current->seq_id == seq_id)
			return current;
		current = current->next;
	}
	return NULL;
}

net_device_t* net_device_find_by_mac(net_devices_list_t* list, const uint8_t* mac)
{
	if (!list) return NULL;

	net_device_t* current = list->head;
	while (current) {
		if (memcmp(current->mac_address, mac, MAC_ADDR_LEN) == 0)
			return current;
		current = current->next;
	}
	return NULL;
}

void net_devices_print(net_devices_list_t* list)
{
	if (!list) return;

	uart_printf("=== Device List (Total: %d) ===\r\n", list->total_anchors);
	net_device_t* current = list->head;
	while (current) {
		uart_printf("  Seq ID: %d, MAC: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
			    current->seq_id,
			    current->mac_address[0], current->mac_address[1],
			    current->mac_address[2], current->mac_address[3],
			    current->mac_address[4], current->mac_address[5]);
		current = current->next;
	}
	uart_puts("==============================\r\n");
}

void net_device_update_distance(net_device_t* device, uint8_t to_seq_id, float distance)
{
	if (!device || to_seq_id >= MAX_DISTANCES) return;
	device->distances[to_seq_id] = distance;
}
