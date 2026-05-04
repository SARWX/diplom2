#ifndef ENUMERATION_H
#define ENUMERATION_H

#include "net_devices.h"
#include "net_mac.h"
#include <stdint.h>

#define ENUM_MAGIC_1         0xDE
#define ENUM_MAGIC_2         0xAD
#define ENUM_MAX_DEVICES     16
#define ENUM_MAX_PACKET_SIZE 128
#define SYNC_WAIT_MS         3000
#define ENUM_LISTEN_MS       6000
#define ENUM_RETRY_MAX       3

/** @brief Run full enumeration as master: DISCOVER → collect → SYNC_LIST → verify OKs. */
int enumeration_start_master(net_devices_list_t* devices);
/** @brief Dispatch an incoming message to the appropriate enumeration handler.
 *  Must be called from main loop context only — never from an ISR. */
void enumeration_handle_message(net_devices_list_t* devices, net_message_t* msg);

/** @brief Returns 1 after a successful enumeration, 0 otherwise. */
uint8_t enumeration_is_complete(void);
/** @brief Returns this device's seq_id as assigned by the master in SYNC_LIST. */
uint8_t enumeration_get_own_seq_id(void);

#endif /* ENUMERATION_H */
