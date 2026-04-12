#ifndef ENUMERATION_H
#define ENUMERATION_H

#include "net_devices.h"
#include "net_mac.h"
#include <stdint.h>

/* Запуск энумерации на главной станции */
int enumeration_start_master(net_devices_list_t* devices);

/* Обработка энумерационных сообщений на любой станции */
void enumeration_handle_message(net_devices_list_t* devices, net_message_t* msg);

/* Получить статус энумерации */
uint8_t enumeration_is_complete(void);

#endif
