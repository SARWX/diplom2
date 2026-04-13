#include "enumeration.h"
#include "net_mac.h"
#include "device_id.h"
#include "cmd_parser.h"
#include "sleep.h"
#include "uart.h"
#include <string.h>
#include <stdlib.h>

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
			net_device_t* device = net_device_create(mac, seq_id, device_type);
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
	uint8_t buffer[ENUM_MAX_PACKET_SIZE];
	uint16_t len;
	
	serialize_device_list(devices, buffer, &len);
	
	/* Отправляем с префиксом SYNC_LIST */
	uint8_t sync_len = cmd_len(CMD_SYNC_LIST);
	uint8_t packet[ENUM_MAX_PACKET_SIZE + sync_len];
	memcpy(packet, cmd_str(CMD_SYNC_LIST), sync_len);
	packet[sync_len] = ' ';  /* Разделитель */
	memcpy(packet + sync_len + 1, buffer, len);
	
	return net_send_to_16bit(dst_addr, packet, sync_len + len);
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
		if (net_send_broadcast((const uint8_t*)cmd_str(
			CMD_DISCOVER), cmd_size(CMD_DISCOVER)) < 0) {
			continue;
		}
		
		/* 3. Ждём ответы */
		net_state.mode = NET_MODE_ENUMERATION;
		dwt_rxenable(DWT_START_RX_IMMEDIATE);
		sleep_ms(ENUM_LISTEN_MS);
		dwt_forcetrxoff();
		
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

 /* DISCOVER от главной станции */
static void handle_discover(net_devices_list_t* devices, net_message_t* msg)
{
	/* Случайная задержка */
	sleep_ms((uint32_t)rand() % 1000);
	
	/* Очищаем свой список перед новой энумерацией */
	net_devices_clear(devices);
	
	/* Отправляем ответ в зависимости от типа устройства */
	const uint8_t* response;
	switch (curr_dev->type)
	{
	case DEVICE_TYPE_ANCHOR:
		response = (const uint8_t*)"A";
		break;
	case DEVICE_TYPE_TAG:
		response = (const uint8_t*)"T";
		break;
	default:
		return;
	}

	if (msg->src_is_eui64)
		net_send_to_64bit(&msg->src_eui64, response, 1);
	else
		net_send_to_16bit(msg->src_addr16, response, 1);
	return;
}

static void handle_sync_list(net_devices_list_t* devices, net_message_t* msg)
{
	net_devices_list_t remote_list;
	net_devices_init(&remote_list);
	
	uint8_t sync_len = cmd_len(CMD_SYNC_LIST);
	sync_len += 1; /* space separator */
	
	if (deserialize_device_list(&remote_list, msg->payload + sync_len, 
					msg->payload_len - sync_len) == 0) {
		cmd_code_t response_cmd = CMD_OK;
		devices->initialized = 1;
		
		if (verify_device_list(devices, &remote_list) != 0) {
			response_cmd = CMD_ERR;
			devices->initialized = 0;
		}
		
		const uint8_t* response = (const uint8_t*)cmd_str(response_cmd);
		uint16_t response_size = cmd_size(response_cmd);
		
		if (msg->src_is_eui64)
			net_send_to_64bit(&msg->src_eui64, response, response_size);
		else
			net_send_to_16bit(msg->src_addr16, response, response_size);
	}
	net_devices_clear(&remote_list);
}

static void handle_ok(net_message_t* msg)
{
	if (net_state.mode == NET_MODE_SYNC_WAIT)
		sync_ok_count++;
}

static void handle_device_response(net_devices_list_t* devices, net_message_t* msg)
{
	uint8_t mac[MAC_ADDR_LEN] = {0};
	uint8_t device_type = (msg->payload[0] == 'A') ?
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
		net_device_t* device = net_device_create(mac, 0, device_type);
		if (device)
			net_device_add(devices, device);
	}
	return;
}


void enumeration_handle_message(net_devices_list_t* devices, net_message_t* msg)
{
	char cmd_buffer[MAX_PAYLOAD_SIZE + 1];
	if (!msg || msg->payload_len == 0) return;

	memcpy(cmd_buffer, msg->payload, msg->payload_len);
	cmd_buffer[msg->payload_len] = '\0';
	
	cmd_parse_result_t result = cmd_parse(cmd_buffer);
	
	switch (result.code) {
		case CMD_DISCOVER:   handle_discover(devices, msg); break;
		case CMD_SYNC_LIST:  handle_sync_list(devices, msg); break;
		case CMD_OK:         handle_ok(msg); break;
		case CMD_ERR:        /* handle error */ break; /* not really needed */
		default:
			if (msg->payload_len >= 1 && (msg->payload[0] == 'A' || 
						msg->payload[0] == 'T')) {
				handle_device_response(devices, msg);
			}
		}
}

uint8_t enumeration_is_complete(void)
{
	return enumeration_complete;
}
