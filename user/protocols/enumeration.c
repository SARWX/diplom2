#include "enumeration.h"
#include "net_mac.h"
#include "device_id.h"
#include "sleep.h"
#include "uart.h"
#include <string.h>
#include <stdlib.h>

#define ENUM_DISCOVER_PAYLOAD ((const uint8_t*)"DSCVR")
#define ENUM_DISCOVER_LEN (sizeof("DSCVR") - 1)

#define ENUM_ANC_RESPONSE_PAYLOAD ((const uint8_t*)"A")
#define ENUM_TAG_RESPONSE_PAYLOAD ((const uint8_t*)"T")
#define ENUM_RESPONSE_LEN (sizeof("X") - 1)

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
static int sync_ok_count = 0;

/*==============================================================================
 * Сериализация списка устройств
 *============================================================================*/

static void serialize_device_list(net_devices_list_t* devices, uint8_t* buffer, uint16_t* len)
{
    uint16_t offset = 0;
    net_device_t* current = devices->head;
    
    /* Магическое число для проверки */
    buffer[offset++] = ENUM_MAGIC_1;
    buffer[offset++] = ENUM_MAGIC_2;
    
    /* Количество устройств */
    buffer[offset++] = devices->total_anchors;
    
    /* Сериализуем каждое устройство */
    while (current && offset < ENUM_MAX_PACKET_SIZE) {
        /* MAC адрес (6 байт) */
        memcpy(buffer + offset, current->mac_address, MAC_ADDR_LEN);
        offset += MAC_ADDR_LEN;
        
        /* seq_id (1 байт) */
        buffer[offset++] = current->seq_id;
        
        /* device_type (1 байт) */
        buffer[offset++] = current->device_type;
        
        current = current->next;
    }
    
    *len = offset;
}

static int deserialize_device_list(net_devices_list_t* devices, const uint8_t* buffer, uint16_t len)
{
    uint16_t offset = 0;
    
    /* Проверка магического числа */
    if (len < 3 || buffer[offset++] != ENUM_MAGIC_1 || buffer[offset++] != ENUM_MAGIC_2) {
        return -1;
    }
    
    uint8_t count = buffer[offset++];
    
    if (count > ENUM_MAX_DEVICES) {
        return -1;
    }
    
    for (int i = 0; i < count && offset + MAC_ADDR_LEN + 2 <= len; i++) {
        uint8_t mac[MAC_ADDR_LEN];
        memcpy(mac, buffer + offset, MAC_ADDR_LEN);
        offset += MAC_ADDR_LEN;
        
        uint8_t seq_id = buffer[offset++];
        uint8_t device_type = buffer[offset++];
        
        /* Проверяем, есть ли уже такое устройство */
        net_device_t* existing = net_device_find_by_mac(devices, mac);
        if (!existing) {
            net_device_t* device = net_device_create(mac, seq_id);
            if (device) {
                device->device_type = device_type;
                net_device_add(devices, device);
            }
        } else if (existing->device_type != device_type) {
            /* Обновляем тип, если он изменился */
            existing->device_type = device_type;
        }
    }
    
    return 0;
}

/*==============================================================================
 * Отправка и проверка списка
 *============================================================================*/

static int send_device_list(net_devices_list_t* devices, net_addr16_t dst_addr)
{
    uint8_t buffer[ENUM_MAX_PACKET_SIZE];
    uint16_t len;
    
    serialize_device_list(devices, buffer, &len);
    
    /* Отправляем с префиксом SYNC */
    uint8_t packet[ENUM_MAX_PACKET_SIZE + ENUM_SYNC_LEN];
    memcpy(packet, ENUM_SYNC_PAYLOAD, ENUM_SYNC_LEN);
    memcpy(packet + ENUM_SYNC_LEN, buffer, len);
    
    return net_send_to_16bit(dst_addr, packet, ENUM_SYNC_LEN + len);
}

static int verify_device_list(net_devices_list_t* local, net_devices_list_t* remote)
{
    if (local->total_anchors != remote->total_anchors) {
        return -1;
    }
    
    net_device_t* local_dev = local->head;
    while (local_dev) {
        net_device_t* remote_dev = net_device_find_by_mac(remote, local_dev->mac_address);
        if (!remote_dev) {
            return -1;
        }
        if (remote_dev->device_type != local_dev->device_type) {
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
        
        /* 5. Отправляем broadcast SYNCLIST один раз */
        net_state.mode = NET_MODE_SYNC_WAIT;
        sync_ok_count = 0;

        send_device_list(devices, NET_BROADCAST_ADDR);  /* broadcast */

        /* Ждём подтверждения */
        sleep_ms(SYNC_WAIT_MS);

        net_state.mode = NET_MODE_IDLE;

        /* 6. Проверяем, что все ответили OK */
        if (sync_ok_count == devices->total_anchors) {
            enumeration_complete = 1;
            devices->initialized = 1;
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
        
        /* Отправляем ответ в зависимости от типа устройства */
        const uint8_t* response;
        switch (curr_dev->type)
        {
        case DEVICE_TYPE_ANCHOR:
            response = ENUM_ANC_RESPONSE_PAYLOAD;
            break;
        case DEVICE_TYPE_TAG:
            response = ENUM_TAG_RESPONSE_PAYLOAD;
            break;
        default:
            return;
        }

        if (msg->src_is_eui64) {
            net_send_to_64bit(&msg->src_eui64, response, ENUM_RESPONSE_LEN);
        } else {
            net_send_to_16bit(msg->src_addr16, response, ENUM_RESPONSE_LEN);
        }
        return;
    }
    
    /* Синхронизация списка от главной станции */
    if (msg->payload_len >= ENUM_SYNC_LEN && 
        memcmp(msg->payload, ENUM_SYNC_PAYLOAD, ENUM_SYNC_LEN) == 0) {
        
        net_devices_list_t remote_list;
        net_devices_init(&remote_list);
        
        if (deserialize_device_list(&remote_list, msg->payload + ENUM_SYNC_LEN, 
                                     msg->payload_len - ENUM_SYNC_LEN) == 0) {
            const uint8_t* response = ENUM_OK_PAYLOAD;
            const uint8_t* response_len = ENUM_OK_LEN;
            devices->initialized = 1;
            if (verify_device_list(devices, &remote_list) != 0) {
                response = ENUM_ERR_PAYLOAD;
                response_len = ENUM_ERR_LEN;
                devices->initialized = 0;
            }
            if (msg->src_is_eui64)
                net_send_to_64bit(&msg->src_eui64, response, response_len);
            else
                net_send_to_16bit(msg->src_addr16, response, response_len);
        }
        net_devices_clear(&remote_list);
        return;
    }
    
    /* Сохраняем ответы от других станций (A или T) */
    if (msg->payload_len >= 1 && 
        (msg->payload[0] == ENUM_ANC_RESPONSE_PAYLOAD ||
                msg->payload[0] == ENUM_TAG_RESPONSE_PAYLOAD)) {
        
        uint8_t mac[MAC_ADDR_LEN] = {0};
        uint8_t device_type = (msg->payload[0] == ENUM_ANC_RESPONSE_PAYLOAD) ?
                DEVICE_TYPE_ANCHOR : DEVICE_TYPE_TAG;
        
        if (msg->src_is_eui64) {
            memcpy(mac, &msg->src_eui64, MAC_ADDR_LEN);
        } else {
            mac[0] = msg->src_addr16 & 0xFF;
            mac[1] = (msg->src_addr16 >> 8) & 0xFF;
        }
        
        /* Добавляем только если нет в списке */
        net_device_t* existing = net_device_find_by_mac(devices, mac);
        if (!existing) {
            net_device_t* device = net_device_create(mac, 0);
            if (device) {
                device->device_type = device_type;
                net_device_add(devices, device);
            }
        } else if (existing->device_type != device_type) {
            existing->device_type = device_type;
        }
        return;
    }

    /* OK от устройства при синхронизации */
    if (net_state.mode == NET_MODE_SYNC_WAIT && 
        msg->payload_len == ENUM_OK_LEN &&
        memcmp(msg->payload, ENUM_OK_PAYLOAD, ENUM_OK_LEN) == 0) {
        sync_ok_count++;
        return;
    }
}

uint8_t enumeration_is_complete(void)
{
    return enumeration_complete;
}
