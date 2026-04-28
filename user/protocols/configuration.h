#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "net_devices.h"
#include "net_mac.h"
#include <stdint.h>

/* Запуск конфигурации на главной станции */
int configuration_start_master(net_devices_list_t* devices);

/* Обработка конфигурационных сообщений на любой станции.
 * Returns 1 if a measurement packet was processed, 0 otherwise. */
int configuration_handle_message(net_devices_list_t* devices, net_message_t* msg);

/* Измерение расстояний до всех устройств в списке — вызывается на анкере */
void configuration_perform_measurements(net_devices_list_t* devices, uint8_t my_seq_id);

/* Отправка накопленных измерений на главную станцию */
void configuration_send_measurements(net_devices_list_t* devices, net_addr16_t dst_addr);

#endif
