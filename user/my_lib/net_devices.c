#include "net_devices.h"
#include "uart.h"
#include <string.h>
#include <stdlib.h>

static uint8_t debug_enabled = 0;

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

net_device_t* net_device_create(const uint8_t* mac, uint8_t seq_id)
{
    net_device_t* device = (net_device_t*)malloc(sizeof(net_device_t));
    if (!device) {
        uart_puts("ERROR: Failed to allocate device\r\n");
        return NULL;
    }
    
    memcpy(device->mac_address, mac, MAC_ADDR_LEN);
    device->seq_id = seq_id;
    device->device_type = DEVICE_TYPE_NONE;
    device->next = NULL;
    
    device->distances = (float*)calloc(MAX_DISTANCES, sizeof(float));
    if (!device->distances) {
        uart_puts("ERROR: Failed to allocate distances\r\n");
        free(device);
        return NULL;
    }
    
    for (int i = 0; i < MAX_DISTANCES; i++) {
        device->distances[i] = -1.0f;
    }
    
    if (debug_enabled) {
        uart_printf("Created device: seq_id=%d, MAC=%02X:%02X:%02X:%02X:%02X:%02X\r\n",
                    seq_id, mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    }
    
    return device;
}

int net_device_add(net_devices_list_t* list, net_device_t* device)
{
    if (!list || !device) return -1;
    
    if (net_device_find_by_mac(list, device->mac_address)) {
        uart_printf("ERROR: Device with MAC already exists\r\n");
        free(device->distances);
        free(device);
        return -2;
    }
    
    device->seq_id = list->total_anchors + 1;
    device->next = list->head;
    list->head = device;
    list->total_anchors++;
    
    if (debug_enabled) {
        uart_printf("Added device seq_id=%d, total=%d\r\n",
                    device->seq_id, list->total_anchors);
    }
    
    return device->seq_id;
}

void net_device_remove(net_devices_list_t* list, uint8_t seq_id)
{
    if (!list || !list->head) return;
    
    net_device_t* current = list->head;
    net_device_t* prev = NULL;
    
    while (current) {
        if (current->seq_id == seq_id) {
            if (prev) {
                prev->next = current->next;
            } else {
                list->head = current->next;
            }
            free(current->distances);
            free(current);
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
            if (prev) {
                prev->next = current->next;
            } else {
                list->head = current->next;
            }
            free(current->distances);
            free(current);
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
        free(current->distances);
        free(current);
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
        if (current->seq_id == seq_id) {
            return current;
        }
        current = current->next;
    }
    return NULL;
}

net_device_t* net_device_find_by_mac(net_devices_list_t* list, const uint8_t* mac)
{
    if (!list) return NULL;
    
    net_device_t* current = list->head;
    while (current) {
        if (memcmp(current->mac_address, mac, MAC_ADDR_LEN) == 0) {
            return current;
        }
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
