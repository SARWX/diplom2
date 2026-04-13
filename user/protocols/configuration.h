#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "net_devices.h"
#include <stdint.h>

/* Запуск конфигурации на главной станции */
int configuration_start_master(net_devices_list_t* devices);

/* Обработка конфигурационных сообщений на любой станции */
void configuration_handle_message(net_devices_list_t* devices, net_message_t* msg);

#endif
