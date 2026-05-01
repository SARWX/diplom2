#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include "net_devices.h"
#include "net_mac.h"
#include <stdint.h>

/** @brief Run full configuration as master: send CMD_CONFIG_START to each anchor and collect measurements. */
int configuration_start_master(net_devices_list_t* devices);

/** @brief Dispatch an incoming message during configuration phase.
 *  Returns 1 if a measurement packet was processed, 0 otherwise. */
int configuration_handle_message(net_devices_list_t* devices, net_message_t* msg);

/** @brief Measure distances to all other devices in the list (anchor side). */
void configuration_perform_measurements(net_devices_list_t* devices, uint8_t my_seq_id);

/** @brief Send accumulated distance measurements to the master. */
void configuration_send_measurements(net_devices_list_t* devices, net_addr16_t dst_addr);

#endif /* CONFIGURATION_H */
