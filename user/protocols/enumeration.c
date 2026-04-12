#include "enumeration.h"
#include "net_mac.h"
#include "sleep.h"
#include "uart.h"
#include <string.h>
#include <stdlib.h>

#define ENUM_DISCOVER_PAYLOAD ((const uint8_t*)"DSCVR")
#define ENUM_DISCOVER_LEN (sizeof("DSCVR") - 1)

#define ENUM_RESPONSE_PAYLOAD ((const uint8_t*)"A")
#define ENUM_RESPONSE_LEN (sizeof("A") - 1)

#define ENUM_SYNC_PAYLOAD ((const uint8_t*)"SYNC")
#define ENUM_SYNC_LEN (sizeof("SYNC") - 1)

#define ENUM_OK_PAYLOAD ((const uint8_t*)"OK")
#define ENUM_OK_LEN (sizeof("OK") - 1)

#define ENUM_ERR_PAYLOAD ((const uint8_t*)"ERR")
#define ENUM_ERR_LEN (sizeof("ERR") - 1)

#define ENUM_LISTEN_MS 2000
#define ENUM_RETRY_MAX 3

static uint8_t enumeration_complete = 0;
static uint8_t retry_count = 0;
static net_devices_list_t* enum_devices = NULL;

/*==============================================================================
 * Сериализация списка устройств
 *============================================================================*/

static void serialize_device_list(net_devices_list_t* devices, uint8_t* buffer, uint16_t* len)
{
    uint16_t offset = 0;
    net_device_t* current = devices->head;
    
    /* Магическое число для проверки */
    buffer[offset++] = 0xDE;
    buffer[offset++] = 0xAD;
    
    /* Количество устройств */
    buffer[offset++] = devices->total_anchors;
    
    /* Сериализуем каждое устройство */
    while (current && offset < 128) {
        /* MAC адрес (6 байт) */
        memcpy(buffer + offset, current->mac_address, 6);
        offset += 6;
        
        /* seq_id (1 байт) */
        buffer[offset++] = current->seq_id;
        
        current = current->next;
    }
    
    *len = offset;
}

static int deserialize_device_list(net_devices_list_t* devices, const uint8_t* buffer, uint16_t len)
{
    uint16_t offset = 0;
    
    /* Проверка магического числа */
    if (len < 3 || buffer[offset++] != 0xDE || buffer[offset++] != 0xAD) {
        return -1;
    }
    
    uint8_t count = buffer[offset++];
    
    for (int i = 0; i < count && offset < len; i++) {
        uint8_t mac[6];
        memcpy(mac, buffer + offset, 6);
        offset += 6;
        
        uint8_t seq_id = buffer[offset++];
        
        /* Проверяем, есть ли уже такое устройство */
        if (!net_device_find_by_mac(devices, mac)) {
            net_device_t* device = net_device_create(mac, seq_id);
            if (device) {
                net_device_add(devices, device);
            }
        }
    }
    
    return 0;
}

/*==============================================================================
 * Отправка и проверка списка
 *============================================================================*/

static int send_device_list(net_devices_list_t* devices, net_addr16_t dst_addr)
{
    uint8_t buffer[128];
    uint16_t len;
    
    serialize_device_list(devices, buffer, &len);
    
    return net_send_to_16bit(dst_addr, buffer, len);
}

static int verify_device_list(net_devices_list_t* local, net_devices_list_t* remote)
{
    if (local->total_anchors != remote->total_anchors) {
        return -1;
    }
    
    net_device_t* local_dev = local->head;
    while (local_dev) {
        if (!net_device_find_by_mac(remote, local_dev->mac_address)) {
            return -1;
        }
        local_dev = local_dev->next;
    }
    
    return 0;
}

/*==============================================================================
 * Главная станция - энумерация
 *============================================================================*/

int enumeration_start_master(net_devices_list_t* devices)
{
    enum_devices = devices;
    enumeration_complete = 0;
    
    uart_puts("\r\n=== Starting ENUMERATION ===\r\n");
    
    for (retry_count = 0; retry_count < ENUM_RETRY_MAX; retry_count++) {
        uart_printf("Attempt %d/%d\r\n", retry_count + 1, ENUM_RETRY_MAX);
        
        /* 1. Очищаем список */
        net_devices_clear(devices);
        
        /* 2. Отправляем DISCOVER */
        dwt_forcetrxoff();
        if (net_send_broadcast(ENUM_DISCOVER_PAYLOAD, ENUM_DISCOVER_LEN) < 0) {
            continue;
        }
        
        /* 3. Ждём ответы */
        net_state.mode = NET_MODE_ENUMERATION;
        dwt_rxenable(DWT_START_RX_IMMEDIATE);
        sleep_ms(ENUM_LISTEN_MS);
        
        /* 4. Если никто не ответил - пробуем ещё */
        if (devices->total_anchors == 0) {
            uart_puts("No devices found, retrying...\r\n");
            continue;
        }
        
        /* 5. Отправляем список всем найденным устройствам */
        uint8_t all_ok = 1;
        net_device_t* current = devices->head;
        while (current) {
            /* Отправляем список */
            send_device_list(devices, current->seq_id);
            
            /* Ждём подтверждение */
            sleep_ms(500);
            
            /* Проверяем, пришёл ли OK от этого устройства */
            /* TODO: реализовать ожидание OK от каждого */
            
            current = current->next;
        }
        
        /* 6. Проверяем, что все ответили OK */
        if (all_ok) {
            enumeration_complete = 1;
            devices->initialized = 1;
            uart_puts("Enumeration completed successfully!\r\n");
            net_devices_print(devices);
            return 0;
        }
        
        uart_puts("Verification failed, retrying...\r\n");
    }
    
    uart_puts("Enumeration failed after all retries\r\n");
    return -1;
}

/*==============================================================================
 * Обработка сообщений энумерации (для любой станции)
 *============================================================================*/

void enumeration_handle_message(net_devices_list_t* devices, net_message_t* msg)
{
    if (!msg || msg->payload_len == 0) return;
    
    /* DISCOVER от главной станции */
    if (msg->payload_len == ENUM_DISCOVER_LEN &&
        memcmp(msg->payload, ENUM_DISCOVER_PAYLOAD, ENUM_DISCOVER_LEN) == 0) {
        
        /* Случайная задержка */
        sleep_ms((uint32_t)rand() % 1000);
        
        /* Очищаем свой список перед новой энумерацией */
        net_devices_clear(devices);
        
        /* Отправляем ответ */
        if (msg->src_is_eui64) {
            net_send_to_64bit(&msg->src_eui64, ENUM_RESPONSE_PAYLOAD, ENUM_RESPONSE_LEN);
        } else {
            net_send_to_16bit(msg->src_addr16, ENUM_RESPONSE_PAYLOAD, ENUM_RESPONSE_LEN);
        }
        return;
    }
    
    /* Синхронизация списка от главной станции */
    if (msg->payload_len >= 3 && memcmp(msg->payload, "SYNC", 4) == 0) {
        net_devices_list_t remote_list;
        net_devices_init(&remote_list);
        
        if (deserialize_device_list(&remote_list, msg->payload + 4, msg->payload_len - 4) == 0) {
            if (verify_device_list(devices, &remote_list) == 0) {
                /* Списки совпадают */
                if (msg->src_is_eui64) {
                    net_send_to_64bit(&msg->src_eui64, ENUM_OK_PAYLOAD, ENUM_OK_LEN);
                } else {
                    net_send_to_16bit(msg->src_addr16, ENUM_OK_PAYLOAD, ENUM_OK_LEN);
                }
                devices->initialized = 1;
            } else {
                /* Списки не совпадают */
                if (msg->src_is_eui64) {
                    net_send_to_64bit(&msg->src_eui64, ENUM_ERR_PAYLOAD, ENUM_ERR_LEN);
                } else {
                    net_send_to_16bit(msg->src_addr16, ENUM_ERR_PAYLOAD, ENUM_ERR_LEN);
                }
            }
        }
        net_devices_clear(&remote_list);
        return;
    }
    
    /* Сохраняем ответы от других станций (A) */
    if (msg->payload_len >= 1 && msg->payload[0] == 'A') {
        uint8_t mac[MAC_ADDR_LEN] = {0};
        
        if (msg->src_is_eui64) {
            memcpy(mac, &msg->src_eui64, MAC_ADDR_LEN);
        } else {
            mac[0] = msg->src_addr16 & 0xFF;
            mac[1] = (msg->src_addr16 >> 8) & 0xFF;
        }
        
        /* Добавляем только если нет в списке */
        if (!net_device_find_by_mac(devices, mac)) {
            net_device_t* device = net_device_create(mac, 0);
            if (device) {
                net_device_add(devices, device);
            }
        }
    }
}

uint8_t enumeration_is_complete(void)
{
    return enumeration_complete;
}
