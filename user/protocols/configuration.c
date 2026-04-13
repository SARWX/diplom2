#include "configuration.h"
#include "net_mac.h"
#include "cmd_parser.h"
#include "uart.h"
#include "sleep.h"
#include <string.h>
#include <stdlib.h>

#define CONFIG_RETRY_MAX    3
#define CONFIG_WAIT_MS      500

/* Сериализация пакета измерений */
typedef struct {
	uint8_t from_seq_id;
	uint8_t to_seq_id;
	float distance;
} measurement_t;

static void serialize_measurement(const measurement_t* m, uint8_t* buffer, uint16_t* len)
{
	buffer[0] = m->from_seq_id;
	buffer[1] = m->to_seq_id;
	memcpy(buffer + 2, &m->distance, sizeof(float));
	*len = 2 + sizeof(float);
}

static int deserialize_measurement(const uint8_t* buffer, uint16_t len, measurement_t* m)
{
	if (len < 2 + sizeof(float)) return -1;
	
	m->from_seq_id = buffer[0];
	m->to_seq_id = buffer[1];
	memcpy(&m->distance, buffer + 2, sizeof(float));
	return 0;
}

/* Отправка измерений от анкера */
static void send_measurements(net_devices_list_t* devices, net_addr16_t dst_addr)
{
	uint8_t buffer[128];
	uint16_t offset = 0;
	net_device_t* current = devices->head;
	
	while (current && offset < 120) {
		for (int i = 1; i <= devices->total_anchors; i++) {
			if (current->distances[i] >= 0) {
				measurement_t m;
				m.from_seq_id = current->seq_id;
				m.to_seq_id = i;
				m.distance = current->distances[i];
				
				uint16_t m_len;
				serialize_measurement(&m, buffer + offset, &m_len);
				offset += m_len;
			}
		}
		current = current->next;
	}
	
	if (offset > 0) {
		net_send_to_16bit(dst_addr, buffer, offset);
	}
}

/* Обработка полученных измерений на главной станции */
static void handle_measurements(net_devices_list_t* devices, const uint8_t* data, uint16_t len)
{
	uint16_t offset = 0;
	
	while (offset + 2 + sizeof(float) <= len) {
		measurement_t m;
		if (deserialize_measurement(data + offset, len - offset, &m) == 0) {
			net_device_t* from = net_device_find_by_seq(devices, m.from_seq_id);
			if (from) {
				net_device_update_distance(from, m.to_seq_id, m.distance);
			}
			offset += 2 + sizeof(float);
		} else {
			break;
		}
	}
}

/*==============================================================================
 * Главная станция - конфигурация
 *============================================================================*/

int configuration_start_master(net_devices_list_t* devices)
{
	if (!devices->initialized || devices->total_anchors == 0) {
		uart_puts("ERROR: System not initialized or no devices\r\n");
		return -1;
	}
	
	uart_puts("\r\n=== Starting CONFIGURATION ===\r\n");
	
	net_device_t* current = devices->head;
	while (current) {
		uart_printf("Configuring device seq_id=%d\r\n", current->seq_id);
		
		for (int retry = 0; retry < CONFIG_RETRY_MAX; retry++) {
			/* Отправляем CONFIG_START устройству */
			net_send_to_16bit(current->seq_id, 
					(const uint8_t*)cmd_str(CMD_CONFIG_START), 
					cmd_size(CMD_CONFIG_START));
			
			/* Ждём измерения */
			sleep_ms(CONFIG_WAIT_MS);
			
			/* Проверяем, пришли ли измерения (обрабатываются в handle_message) */
			/* Здесь нужен флаг, что измерения получены */
			if (1) { /* TODO: проверять флаг measurements_received */
				break;
			}
			
			uart_printf("Retry %d/%d for seq_id=%d\r\n", retry + 1, CONFIG_RETRY_MAX, current->seq_id);
		}
		
		/* Отправляем CONFIG_STOP */
		net_send_to_16bit(current->seq_id, 
				(const uint8_t*)cmd_str(CMD_CONFIG_STOP), 
				cmd_len(CMD_CONFIG_STOP));
		
		current = current->next;
		sleep_ms(100);
	}
	
	uart_puts("Configuration completed\r\n");
	return 0;
}

/*==============================================================================
 * Обработка сообщений (для любой станции)
 *============================================================================*/

void configuration_handle_message(net_devices_list_t* devices, net_message_t* msg)
{
	if (!msg || msg->payload_len == 0) return;
	
	char cmd_buffer[64];
	memcpy(cmd_buffer, msg->payload, msg->payload_len);
	cmd_buffer[msg->payload_len] = '\0';
	
	cmd_parse_result_t result = cmd_parse(cmd_buffer);
	
	switch (result.code) {
		case CMD_CONFIG_START:
			/* Анкер получил запрос на конфигурацию */
			net_state.mode = NET_MODE_CONFIG;
			/* ТУТ надо НАПИСАТЬ ФУНКЦИЮ ИЗМЕРЕНИЙ  РАССТОЯНИЙ */
			send_measurements(devices, msg->src_addr16);
			break;
		
		case CMD_CONFIG_STOP:
			/* Анкер получил останов конфигурации */
			net_state.mode = NET_MODE_IDLE;
			break;
		
		default:
			/* Возможно, это пакет с измерениями */
			handle_measurements(devices, msg->payload, msg->payload_len);
			break;
		}
}
