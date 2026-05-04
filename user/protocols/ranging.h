#ifndef RANGING_H
#define RANGING_H

/**
 * @file ranging.h
 * @brief Tag-side ranging protocol.
 *
 * Flow:
 *  1. Master broadcasts CMD_RANGING_START.
 *  2. Tag receives it → enters NET_MODE_RANGING → loops:
 *       measure all anchors → send meas_table row to master → delay.
 *  3. Tag exits loop on CMD_RANGING_STOP or CMD_STOP broadcast.
 *  4. Anchors ignore CMD_RANGING_START (TWR responder always active via ISR).
 *  5. Master receives meas_table packets via ranging_handle_rx(),
 *     prints them and forwards binary over UART.
 */

#include "net_devices.h"
#include "net_mac.h"
#include <stdint.h>

/**
 * @brief Run the continuous ranging loop (tag side).
 *
 * Blocks until CMD_RANGING_STOP or CMD_STOP is received.
 * Sets net_state.mode = NET_MODE_RANGING on entry, NET_MODE_IDLE on exit.
 *
 * @param devices      The tag's device list.
 * @param own_seq_id   This tag's seq_id.
 */
void ranging_run(net_devices_list_t* devices, uint8_t own_seq_id);

/**
 * @brief Dispatch an incoming message to ranging_run() if appropriate (tag side).
 *
 * Recognizes CMD_RANGING_START and calls ranging_run().
 * Must be called from main loop context only.
 */
void ranging_handle_message(net_devices_list_t* devices, net_message_t* msg,
                             uint8_t own_seq_id);

/**
 * @brief Handle an incoming meas_table packet on the master (main anchor side).
 *
 * Deserializes the binary measurement packet from a tag and updates the
 * master's device list. Prints the received row via uart_dbg() and sends
 * the binary packet over UART for the host application.
 *
 * @param devices  Master's device list.
 * @param msg      Incoming message containing a meas_table packet.
 * @return 1 if a valid measurement packet was processed, 0 otherwise.
 */
int ranging_handle_rx(net_devices_list_t* devices, net_message_t* msg);

#endif /* RANGING_H */
