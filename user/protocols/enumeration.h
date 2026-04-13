#ifndef ENUMERATION_H
#define ENUMERATION_H

#include "net_devices.h"
#include "net_mac.h"
#include <stdint.h>

/* Константы протокола энумерации */
#define ENUM_MAGIC_1        0xDE
#define ENUM_MAGIC_2        0xAD
#define ENUM_MAX_DEVICES    16
#define ENUM_MAX_PACKET_SIZE 128
#define SYNC_WAIT_MS        1000
#define ENUM_LISTEN_MS 2000
#define ENUM_RETRY_MAX 3

/* Запуск энумерации на главной станции */
int enumeration_start_master(net_devices_list_t* devices);

/* Обработка энумерационных сообщений на любой станции */
void enumeration_handle_message(net_devices_list_t* devices, net_message_t* msg);

/* Получить статус энумерации */
uint8_t enumeration_is_complete(void);

#endif
